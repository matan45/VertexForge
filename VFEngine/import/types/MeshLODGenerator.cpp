#include "print/Log.hpp"
#include "MeshLODGenerator.hpp"
#include "threading/JobSystem.hpp"

#include <algorithm>
#include <cfloat>
#include <meshoptimizer.h>

#define ENABLE_VHACD_IMPLEMENTATION 1
#include <VHACD.h>
#include "VHACDCallback.hpp"

namespace
{
    importConfig::MeshImportConfig applyVHACDPreset(const importConfig::MeshImportConfig& config)
    {
        if (config.vhacdPreset == importConfig::VHACDPreset::Custom)
        {
            return config;
        }

        importConfig::MeshImportConfig result = config;

        switch (config.vhacdPreset)
        {
            case importConfig::VHACDPreset::Fast:
                result.vhacdResolution = 50000;
                result.maxConvexHulls = 8;
                result.maxVerticesPerHull = 32;
                result.minVolumePercentError = 5.0f;
                result.maxRecursionDepth = 8;
                break;
            case importConfig::VHACDPreset::Balanced:
                result.vhacdResolution = 100000;
                result.maxConvexHulls = 16;
                result.maxVerticesPerHull = 64;
                result.minVolumePercentError = 1.0f;
                result.maxRecursionDepth = 10;
                break;
            case importConfig::VHACDPreset::Quality:
                result.vhacdResolution = 200000;
                result.maxConvexHulls = 32;
                result.maxVerticesPerHull = 128;
                result.minVolumePercentError = 0.5f;
                result.maxRecursionDepth = 12;
                break;
            default:
                break;
        }
        return result;
    }

    VHACD::IVHACD::Parameters buildVHACDParams(const importConfig::MeshImportConfig& effectiveConfig,
                                                VHACD::IVHACD::IUserCallback* callback)
    {
        VHACD::IVHACD::Parameters params;
        params.m_maxConvexHulls = effectiveConfig.maxConvexHulls;
        params.m_resolution = effectiveConfig.vhacdResolution;
        params.m_maxNumVerticesPerCH = effectiveConfig.maxVerticesPerHull;
        params.m_minimumVolumePercentErrorAllowed = static_cast<double>(effectiveConfig.minVolumePercentError);
        params.m_maxRecursionDepth = effectiveConfig.maxRecursionDepth;
        params.m_shrinkWrap = effectiveConfig.shrinkWrap;
        params.m_asyncACD = false;
        params.m_callback = callback;
        return params;
    }

    void extractConvexHulls(VHACD::IVHACD* vhacd,
                            const importConfig::MeshImportConfig& effectiveConfig,
                            resource::ConvexDecompositionData& result)
    {
        uint32_t numHulls = vhacd->GetNConvexHulls();
        vfLogInfo("  V-HACD generated {} convex hulls", numHulls);

        result.hasDecomposition = true;
        result.params.maxConvexHulls = effectiveConfig.maxConvexHulls;
        result.params.resolution = effectiveConfig.vhacdResolution;
        result.params.maxVerticesPerHull = effectiveConfig.maxVerticesPerHull;
        result.params.minVolumePercentError = effectiveConfig.minVolumePercentError;
        result.params.maxRecursionDepth = effectiveConfig.maxRecursionDepth;

        constexpr uint32_t joltMaxVertices = 256;
        const uint32_t effectiveMaxVertices = std::min(effectiveConfig.maxVerticesPerHull, joltMaxVertices);

        uint32_t skippedHulls = 0;

        for (uint32_t i = 0; i < numHulls; ++i)
        {
            VHACD::IVHACD::ConvexHull hull;
            vhacd->GetConvexHull(i, hull);

            if (hull.m_points.size() > effectiveMaxVertices)
            {
                vfLogWarning("  Hull {} has {} vertices (exceeds limit of {}), skipping",
                             i, hull.m_points.size(), effectiveMaxVertices);
                ++skippedHulls;
                continue;
            }

            if (hull.m_points.empty())
            {
                ++skippedHulls;
                continue;
            }

            result.hulls.emplace_back();
            auto& outHull = result.hulls.back();
            outHull.vertices.reserve(hull.m_points.size());

            for (const auto& p : hull.m_points)
            {
                outHull.vertices.emplace_back(
                    static_cast<float>(p.mX),
                    static_cast<float>(p.mY),
                    static_cast<float>(p.mZ)
                );
            }

            outHull.indices.reserve(hull.m_triangles.size() * 3);
            for (const auto& tri : hull.m_triangles)
            {
                outHull.indices.push_back(tri.mI0);
                outHull.indices.push_back(tri.mI1);
                outHull.indices.push_back(tri.mI2);
            }

            outHull.center = glm::vec3(
                static_cast<float>(hull.m_center.GetX()),
                static_cast<float>(hull.m_center.GetY()),
                static_cast<float>(hull.m_center.GetZ())
            );
            outHull.volume = static_cast<float>(hull.m_volume);
        }

        if (skippedHulls > 0)
        {
            vfLogWarning("  Skipped {} hulls due to vertex count limits", skippedHulls);
        }

        if (result.hulls.empty())
        {
            vfLogWarning("  All hulls were skipped, decomposition invalid");
            result.hasDecomposition = false;
        }
        else
        {
            vfLogInfo("  Final hull count: {}, total vertices: {}",
                      result.hulls.size(), result.getTotalVertexCount());
        }
    }

    void compactSimplifiedMesh(types::LODMeshData& result, const types::LODMeshData& source)
    {
        std::vector<unsigned int> remap(source.vertices.size(), ~0u);
        size_t uniqueVertexCount = 0;

        for (size_t i = 0; i < result.indices.size(); ++i)
        {
            uint32_t idx = result.indices[i];
            if (remap[idx] == ~0u)
            {
                remap[idx] = static_cast<unsigned int>(uniqueVertexCount++);
            }
        }

        result.vertices.resize(uniqueVertexCount);
        for (size_t i = 0; i < source.vertices.size(); ++i)
        {
            if (remap[i] != ~0u)
            {
                result.vertices[remap[i]] = source.vertices[i];
            }
        }

        for (size_t i = 0; i < result.indices.size(); ++i)
        {
            result.indices[i] = remap[result.indices[i]];
        }
    }

    void packMeshletPrimitives(const std::vector<meshopt_Meshlet>& meshoptMeshlets,
                               const std::vector<unsigned char>& meshletTriangleIndices,
                               size_t meshletCount,
                               types::MeshletBuildResult& result)
    {
        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];

            if (m.vertex_count > 255 || m.triangle_count > 255)
            {
                vfLogError("Meshlet {} has invalid counts: vertices={}, triangles={} (max 255)",
                           i, m.vertex_count, m.triangle_count);
                return;
            }

            for (unsigned int t = 0; t < m.triangle_count; ++t)
            {
                size_t triOffset = m.triangle_offset + t * 3;

                unsigned char idx0 = meshletTriangleIndices[triOffset + 0];
                unsigned char idx1 = meshletTriangleIndices[triOffset + 1];
                unsigned char idx2 = meshletTriangleIndices[triOffset + 2];

                if (idx0 >= m.vertex_count || idx1 >= m.vertex_count || idx2 >= m.vertex_count)
                {
                    vfLogError("Meshlet {} triangle {} has out-of-bounds index: [{},{},{}] >= vertex_count {}",
                               i, t, idx0, idx1, idx2, m.vertex_count);
                    return;
                }

                uint32_t packed =
                    static_cast<uint32_t>(idx0) |
                    (static_cast<uint32_t>(idx1) << 8) |
                    (static_cast<uint32_t>(idx2) << 16);
                result.meshletPrimitives.push_back(packed);
            }
        }
    }

    void computeMeshletDescriptorsAndBounds(const std::vector<meshopt_Meshlet>& meshoptMeshlets,
                                            const std::vector<unsigned int>& meshletVertexIndices,
                                            const std::vector<unsigned char>& meshletTriangleIndices,
                                            size_t meshletCount,
                                            const types::LODMeshData& lodMesh,
                                            types::MeshletBuildResult& result)
    {
        uint32_t primitiveOffset = 0;
        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];
            auto& outMeshlet = result.meshlets[i];

            outMeshlet.descriptor.vertexOffset = m.vertex_offset;
            outMeshlet.descriptor.primitiveOffset = primitiveOffset;
            outMeshlet.descriptor.vertexCount = static_cast<uint8_t>(m.vertex_count);
            outMeshlet.descriptor.primitiveCount = static_cast<uint8_t>(m.triangle_count);
            outMeshlet.descriptor.padding = 0;

            primitiveOffset += m.triangle_count;

            meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                &meshletVertexIndices[m.vertex_offset],
                &meshletTriangleIndices[m.triangle_offset],
                m.triangle_count,
                reinterpret_cast<const float*>(lodMesh.vertices.data()),
                lodMesh.vertices.size(),
                sizeof(resource::Vertex)
            );

            outMeshlet.bounds.boundingSphere = glm::vec4(
                bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius
            );

            outMeshlet.bounds.cone = glm::vec4(
                bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2],
                bounds.cone_cutoff
            );
        }
    }
}

namespace types
{
    LODMeshData MeshLODGenerator::simplifyMesh(const LODMeshData& source, float targetRatio) const
    {
        if (source.indices.empty() || source.vertices.empty() || targetRatio >= 1.0f)
        {
            return source;
        }

        size_t targetIndexCount = static_cast<size_t>(source.indices.size() * targetRatio);
        targetIndexCount = std::max(targetIndexCount, static_cast<size_t>(3));
        targetIndexCount = (targetIndexCount / 3) * 3;

        LODMeshData result;
        result.indices.resize(source.indices.size());

        size_t actualIndexCount = meshopt_simplifySloppy(
            result.indices.data(),
            source.indices.data(),
            source.indices.size(),
            reinterpret_cast<const float*>(source.vertices.data()),
            source.vertices.size(),
            sizeof(resource::Vertex),
            targetIndexCount,
            FLT_MAX,
            nullptr
        );

        result.indices.resize(actualIndexCount);

        if (actualIndexCount == source.indices.size())
        {
            vfLogWarning("  Simplification failed for ratio {:.1f}%, keeping original", targetRatio * 100.0f);
            return source;
        }

        meshopt_optimizeVertexCache(
            result.indices.data(),
            result.indices.data(),
            result.indices.size(),
            source.vertices.size()
        );

        compactSimplifiedMesh(result, source);

        return result;
    }

    std::array<LODMeshData, resource::LOD_LEVEL_COUNT> MeshLODGenerator::generateLODLevels(
        const LODMeshData& lod0) const
    {
        std::array<LODMeshData, resource::LOD_LEVEL_COUNT> lodLevels;

        lodLevels[0] = lod0;

        // Each LOD simplifies from lod0 independently — parallelize
        std::vector<std::future<LODMeshData>> futures;
        futures.reserve(resource::LOD_LEVEL_COUNT - 1);

        for (uint32_t level = 1; level < resource::LOD_LEVEL_COUNT; ++level)
        {
            float ratio = lodRatios[level];
            futures.push_back(threading::JobSystem::instance().submit(
                [this, &lod0, ratio]() -> LODMeshData
                {
                    return simplifyMesh(lod0, ratio);
                }, threading::JobPriority::NORMAL
            ));
        }

        for (uint32_t level = 1; level < resource::LOD_LEVEL_COUNT; ++level)
        {
            lodLevels[level] = futures[level - 1].get();

            vfLogInfo("  LOD{}: {} vertices, {} triangles ({}%)",
                      level,
                      lodLevels[level].vertices.size(),
                      lodLevels[level].indices.size() / 3,
                      static_cast<int>(lodRatios[level] * 100));
        }

        return lodLevels;
    }

    MeshletBuildResult MeshLODGenerator::buildMeshletsForLOD(const LODMeshData& lodMesh) const
    {
        MeshletBuildResult result;

        if (lodMesh.indices.empty() || lodMesh.vertices.empty())
        {
            return result;
        }

        const size_t maxMeshlets = meshopt_buildMeshletsBound(
            lodMesh.indices.size(),
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES
        );

        std::vector<meshopt_Meshlet> meshoptMeshlets(maxMeshlets);
        std::vector<unsigned int> meshletVertexIndices(maxMeshlets * resource::MAX_MESHLET_VERTICES);
        std::vector<unsigned char> meshletTriangleIndices(maxMeshlets * resource::MAX_MESHLET_PRIMITIVES * 3);

        size_t meshletCount = meshopt_buildMeshlets(
            meshoptMeshlets.data(),
            meshletVertexIndices.data(),
            meshletTriangleIndices.data(),
            lodMesh.indices.data(),
            lodMesh.indices.size(),
            reinterpret_cast<const float*>(lodMesh.vertices.data()),
            lodMesh.vertices.size(),
            sizeof(resource::Vertex),
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES,
            0.0f
        );

        if (meshletCount == 0)
        {
            return result;
        }

        const auto& lastMeshlet = meshoptMeshlets[meshletCount - 1];
        size_t totalVertexIndices = lastMeshlet.vertex_offset + lastMeshlet.vertex_count;
        size_t totalTriangleIndices = lastMeshlet.triangle_offset + ((lastMeshlet.triangle_count * 3 + 3) & ~3);

        meshoptMeshlets.resize(meshletCount);
        meshletVertexIndices.resize(totalVertexIndices);
        meshletTriangleIndices.resize(totalTriangleIndices);

        result.meshlets.resize(meshletCount);
        result.meshletVertices.resize(totalVertexIndices);
        result.meshletPrimitives.reserve((totalTriangleIndices + 3) / 4);

        for (size_t i = 0; i < totalVertexIndices; ++i)
        {
            result.meshletVertices[i] = meshletVertexIndices[i];
        }

        packMeshletPrimitives(meshoptMeshlets, meshletTriangleIndices, meshletCount, result);

        computeMeshletDescriptorsAndBounds(meshoptMeshlets, meshletVertexIndices,
                                           meshletTriangleIndices, meshletCount, lodMesh, result);

        vfLogInfo("    Generated {} meshlets ({} vertex indices, {} primitives)",
                  meshletCount, totalVertexIndices, result.meshletPrimitives.size());

        return result;
    }

    resource::ConvexDecompositionData MeshLODGenerator::generateConvexDecomposition(
        const LODMeshData& meshData,
        const importConfig::MeshImportConfig& config,
        ConvexProgressCallback progressCallback,
        std::atomic<bool>* cancelFlag) const
    {
        resource::ConvexDecompositionData result;

        if (!config.generateConvexDecomposition || meshData.vertices.empty() || meshData.indices.empty())
        {
            return result;
        }

        auto effectiveConfig = applyVHACDPreset(config);

        vfLogInfo("  Running V-HACD convex decomposition (preset: {}, resolution: {})...",
                  static_cast<int>(config.vhacdPreset), effectiveConfig.vhacdResolution);

        std::vector<double> points;
        points.reserve(meshData.vertices.size() * 3);
        for (const auto& v : meshData.vertices)
        {
            points.push_back(static_cast<double>(v.position.x));
            points.push_back(static_cast<double>(v.position.y));
            points.push_back(static_cast<double>(v.position.z));
        }

        VHACDCallback callback([&](float progress, std::string_view stage, std::string_view /*operation*/) {
            if (progressCallback)
            {
                progressCallback(progress, stage);
            }
        });

        auto params = buildVHACDParams(effectiveConfig, &callback);

        VHACD::IVHACD* vhacd = VHACD::CreateVHACD();

        bool success = vhacd->Compute(
            points.data(),
            static_cast<uint32_t>(meshData.vertices.size()),
            meshData.indices.data(),
            static_cast<uint32_t>(meshData.indices.size() / 3),
            params
        );

        if (cancelFlag && cancelFlag->load())
        {
            vfLogInfo("  V-HACD decomposition cancelled");
            vhacd->Release();
            return result;
        }

        if (success)
        {
            extractConvexHulls(vhacd, effectiveConfig, result);
        }
        else
        {
            vfLogWarning("  V-HACD decomposition failed");
        }

        vhacd->Release();
        return result;
    }
}
