#include "Mesh.hpp"
#include "print/EditorLogger.hpp"
#include "resource/EndianUtils.hpp"

#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cfloat>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <meshoptimizer.h>

// V-HACD for convex decomposition (header-only, implementation in this TU)
#define ENABLE_VHACD_IMPLEMENTATION 1
#include <VHACD.h>

namespace types
{
    void Mesh::loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                            std::string_view location, MeshProgressCallback progressCallback) const
    {
        if (progressCallback) progressCallback(0.0f);

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(file.path.data(),
                                                 aiProcess_Triangulate | aiProcess_FlipUVs |
                                                 aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            vfLogError("Failed to load Mesh file: {}", importer.GetErrorString());
            return;
        }

        if (progressCallback) progressCallback(0.2f);

        saveToFileStreamingWithLOD(location, fileName, scene, file.config, progressCallback);

        if (progressCallback) progressCallback(1.0f);
    }

    LODMeshData Mesh::convertAssimpMesh(const aiMesh* assimpMesh) const
    {
        LODMeshData result;
        result.vertices.reserve(assimpMesh->mNumVertices);

        // Convert vertices
        for (unsigned int v = 0; v < assimpMesh->mNumVertices; ++v)
        {
            resource::Vertex vertex;
            vertex.position = {
                assimpMesh->mVertices[v].x,
                assimpMesh->mVertices[v].y,
                assimpMesh->mVertices[v].z
            };

            if (assimpMesh->HasNormals())
            {
                vertex.normal = {
                    assimpMesh->mNormals[v].x,
                    assimpMesh->mNormals[v].y,
                    assimpMesh->mNormals[v].z
                };
            }
            else
            {
                vertex.normal = {0.0f, 0.0f, 0.0f};
            }

            if (assimpMesh->mTextureCoords[0])
            {
                vertex.texCoords = {
                    assimpMesh->mTextureCoords[0][v].x,
                    assimpMesh->mTextureCoords[0][v].y
                };
            }
            else
            {
                vertex.texCoords = {0.0f, 0.0f};
            }

            result.vertices.push_back(vertex);
        }

        uint32_t totalIndices = 0;
        for (unsigned int f = 0; f < assimpMesh->mNumFaces; ++f)
        {
            totalIndices += assimpMesh->mFaces[f].mNumIndices;
        }
        result.indices.reserve(totalIndices);

        for (unsigned int f = 0; f < assimpMesh->mNumFaces; ++f)
        {
            const aiFace& face = assimpMesh->mFaces[f];
            for (unsigned int k = 0; k < face.mNumIndices; ++k)
            {
                result.indices.push_back(face.mIndices[k]);
            }
        }

        return result;
    }

    LODMeshData Mesh::simplifyMesh(const LODMeshData& source, float targetRatio) const
    {
        if (source.indices.empty() || source.vertices.empty())
        {
            return source;
        }

        // If ratio is 1.0, just return a copy
        if (targetRatio >= 1.0f)
        {
            return source;
        }

        size_t targetIndexCount = static_cast<size_t>(source.indices.size() * targetRatio);
        // Ensure at least 3 indices (one triangle)
        targetIndexCount = std::max(targetIndexCount, static_cast<size_t>(3));
        // Round to multiple of 3
        targetIndexCount = (targetIndexCount / 3) * 3;

        LODMeshData result;
        result.indices.resize(source.indices.size()); // Allocate max size initially

        size_t actualIndexCount = meshopt_simplifySloppy(
            result.indices.data(),
            source.indices.data(),
            source.indices.size(),
            reinterpret_cast<const float*>(source.vertices.data()),
            source.vertices.size(),
            sizeof(resource::Vertex),
            targetIndexCount,
            FLT_MAX, // No error limit - allow maximum simplification
            nullptr // No result error output needed
        );

        result.indices.resize(actualIndexCount);

        if (actualIndexCount == source.indices.size())
        {
            vfLogWarning("  Simplification failed for ratio {:.1f}%, keeping original", targetRatio * 100.0f);
            return source;
        }

        // Optimize vertex cache for better GPU performance
        meshopt_optimizeVertexCache(
            result.indices.data(),
            result.indices.data(),
            result.indices.size(),
            source.vertices.size()
        );

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

        // Create compacted vertex buffer
        result.vertices.resize(uniqueVertexCount);
        for (size_t i = 0; i < source.vertices.size(); ++i)
        {
            if (remap[i] != ~0u)
            {
                result.vertices[remap[i]] = source.vertices[i];
            }
        }

        // Remap indices to use new vertex indices
        for (size_t i = 0; i < result.indices.size(); ++i)
        {
            result.indices[i] = remap[result.indices[i]];
        }

        return result;
    }

    std::array<LODMeshData, resource::LOD_LEVEL_COUNT> Mesh::generateLODLevels(const LODMeshData& lod0) const
    {
        std::array<LODMeshData, resource::LOD_LEVEL_COUNT> lodLevels;

        // LOD0: Original mesh (100%)
        lodLevels[0] = lod0;

        // Generate LOD1, LOD2, LOD3 by simplifying from LOD0
        for (uint32_t level = 1; level < resource::LOD_LEVEL_COUNT; ++level)
        {
            lodLevels[level] = simplifyMesh(lod0, lodRatios[level]);

            vfLogInfo("  LOD{}: {} vertices, {} triangles ({}%)",
                      level,
                      lodLevels[level].vertices.size(),
                      lodLevels[level].indices.size() / 3,
                      static_cast<int>(lodRatios[level] * 100));
        }

        return lodLevels;
    }

    MeshletBuildResult Mesh::buildMeshletsForLOD(const LODMeshData& lodMesh) const
    {
        MeshletBuildResult result;

        if (lodMesh.indices.empty() || lodMesh.vertices.empty())
        {
            return result;
        }

        // Calculate maximum number of meshlets needed
        const size_t maxMeshlets = meshopt_buildMeshletsBound(
            lodMesh.indices.size(),
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES
        );

        // Allocate temporary buffers for meshoptimizer output
        std::vector<meshopt_Meshlet> meshoptMeshlets(maxMeshlets);
        std::vector<unsigned int> meshletVertexIndices(maxMeshlets * resource::MAX_MESHLET_VERTICES);
        std::vector<unsigned char> meshletTriangleIndices(maxMeshlets * resource::MAX_MESHLET_PRIMITIVES * 3);

        // Build meshlets
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
            0.0f // cone_weight: 0 = balanced locality
        );

        if (meshletCount == 0)
        {
            return result;
        }

        // Trim to actual size
        const auto& lastMeshlet = meshoptMeshlets[meshletCount - 1];
        size_t totalVertexIndices = lastMeshlet.vertex_offset + lastMeshlet.vertex_count;
        size_t totalTriangleIndices = lastMeshlet.triangle_offset + ((lastMeshlet.triangle_count * 3 + 3) & ~3);

        meshoptMeshlets.resize(meshletCount);
        meshletVertexIndices.resize(totalVertexIndices);
        meshletTriangleIndices.resize(totalTriangleIndices);

        // Convert to our format and compute bounds
        result.meshlets.resize(meshletCount);
        result.meshletVertices.resize(totalVertexIndices);
        result.meshletPrimitives.reserve((totalTriangleIndices + 3) / 4);

        // Copy vertex indices
        for (size_t i = 0; i < totalVertexIndices; ++i)
        {
            result.meshletVertices[i] = meshletVertexIndices[i];
        }

        // Pack triangle indices (3 uint8 per triangle -> 1 uint32 per triangle)
        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];
            for (unsigned int t = 0; t < m.triangle_count; ++t)
            {
                size_t triOffset = m.triangle_offset + t * 3;
                uint32_t packed =
                    static_cast<uint32_t>(meshletTriangleIndices[triOffset + 0]) |
                    (static_cast<uint32_t>(meshletTriangleIndices[triOffset + 1]) << 8) |
                    (static_cast<uint32_t>(meshletTriangleIndices[triOffset + 2]) << 16);
                result.meshletPrimitives.push_back(packed);
            }
        }

        // Convert meshlets and compute bounds
        uint32_t primitiveOffset = 0;
        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];
            auto& outMeshlet = result.meshlets[i];

            // Set descriptor
            outMeshlet.descriptor.vertexOffset = m.vertex_offset;
            outMeshlet.descriptor.primitiveOffset = primitiveOffset;
            outMeshlet.descriptor.vertexCount = static_cast<uint8_t>(m.vertex_count);
            outMeshlet.descriptor.primitiveCount = static_cast<uint8_t>(m.triangle_count);
            outMeshlet.descriptor.padding = 0;

            primitiveOffset += m.triangle_count;

            // Compute bounding sphere and cone
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

            // Cone for backface culling (cutoff >= 1.0 means no backface culling)
            outMeshlet.bounds.cone = glm::vec4(
                bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2],
                bounds.cone_cutoff
            );
        }

        vfLogInfo("    Generated {} meshlets ({} vertex indices, {} primitives)",
                  meshletCount, totalVertexIndices, result.meshletPrimitives.size());

        return result;
    }

    void Mesh::writeMeshletData(std::ofstream& outFile,
                                const std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT>& meshletResults) const
    {
        // Write per-LOD meshlet info header
        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            const auto& result = meshletResults[lod];
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshlets.size()));
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshletVertices.size()));
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshletPrimitives.size()));
        }

        // Write all meshlet descriptors and bounds
        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (const auto& meshlet : meshletResults[lod].meshlets)
            {
                // Write descriptor (12 bytes)
                resource::endian::writeLE<uint32_t>(outFile, meshlet.descriptor.vertexOffset);
                resource::endian::writeLE<uint32_t>(outFile, meshlet.descriptor.primitiveOffset);
                resource::endian::writeLE<uint8_t>(outFile, meshlet.descriptor.vertexCount);
                resource::endian::writeLE<uint8_t>(outFile, meshlet.descriptor.primitiveCount);
                resource::endian::writeLE<uint16_t>(outFile, 0); // padding

                // Write bounds (32 bytes)
                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.x);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.y);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.z);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.w);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.x);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.y);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.z);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.w);
            }
        }

        // Write all meshlet vertex indices
        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (uint32_t idx : meshletResults[lod].meshletVertices)
            {
                resource::endian::writeLE<uint32_t>(outFile, idx);
            }
        }

        // Write all meshlet primitive data (packed)
        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (uint32_t packed : meshletResults[lod].meshletPrimitives)
            {
                resource::endian::writeLE<uint32_t>(outFile, packed);
            }
        }
    }

    void Mesh::writeLODLevel(std::ofstream& outFile, const LODMeshData& lodMesh) const
    {
        constexpr size_t verticesPerChunk = chunkSize / sizeof(resource::Vertex);

        // Write vertex count
        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(lodMesh.vertices.size()));

        // Write vertices in chunks
        for (size_t v = 0; v < lodMesh.vertices.size(); v += verticesPerChunk)
        {
            size_t chunkEnd = std::min(v + verticesPerChunk, lodMesh.vertices.size());

            for (size_t j = v; j < chunkEnd; ++j)
            {
                const auto& vertex = lodMesh.vertices[j];
                resource::endian::writeLE<float>(outFile, vertex.position.x);
                resource::endian::writeLE<float>(outFile, vertex.position.y);
                resource::endian::writeLE<float>(outFile, vertex.position.z);
                resource::endian::writeLE<float>(outFile, vertex.normal.x);
                resource::endian::writeLE<float>(outFile, vertex.normal.y);
                resource::endian::writeLE<float>(outFile, vertex.normal.z);
                resource::endian::writeLE<float>(outFile, vertex.texCoords.x);
                resource::endian::writeLE<float>(outFile, vertex.texCoords.y);
            }
        }

        // Write index count
        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(lodMesh.indices.size()));

        // Write indices in chunks
        constexpr size_t indicesPerChunk = chunkSize / sizeof(uint32_t);
        for (size_t i = 0; i < lodMesh.indices.size(); i += indicesPerChunk)
        {
            size_t chunkEnd = std::min(i + indicesPerChunk, lodMesh.indices.size());
            std::vector<uint32_t> indexChunk(lodMesh.indices.begin() + i, lodMesh.indices.begin() + chunkEnd);
            resource::endian::writeVectorLE<uint32_t>(outFile, indexChunk);
        }
    }

    void Mesh::saveToFileStreamingWithLOD(std::string_view location, std::string_view fileName,
                                          const aiScene* scene, const importConfig::ImportConfig& config,
                                          MeshProgressCallback progressCallback) const
    {
        std::filesystem::path newFileLocation = std::filesystem::path(location) / (std::string(fileName) + "." +
            FileExtension::mesh);
        std::ofstream outFile(newFileLocation, std::ios::binary);

        if (!outFile)
        {
            vfLogError("Failed to open file for writing: {}", newFileLocation.string());
            return;
        }

        // Write header with version 0.0.5 (LOD + Meshlet + Convex Decomposition support)
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(resource::FileType::MESH));
        resource::endian::writeLE<uint32_t>(outFile, 0); // major
        resource::endian::writeLE<uint32_t>(outFile, 0); // minor
        resource::endian::writeLE<uint32_t>(outFile, 5); // patch - version 0.0.5 for convex decomposition
        resource::endian::writeLE<uint32_t>(outFile, scene->mNumMeshes);

        vfLogInfo("Generating LODs and meshlets for {} submeshes...", scene->mNumMeshes);

        // Process each mesh
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            const aiMesh* assimpMesh = scene->mMeshes[i];
            std::string meshName = assimpMesh->mName.C_Str();

            vfLogInfo("Processing submesh '{}' ({} vertices, {} triangles)...",
                      meshName,
                      assimpMesh->mNumVertices,
                      assimpMesh->mNumFaces);

            // Write submesh name
            uint32_t nameLength = static_cast<uint32_t>(meshName.length());
            resource::endian::writeLE<uint32_t>(outFile, nameLength);
            if (nameLength > 0)
            {
                outFile.write(meshName.data(), nameLength);
            }

            // Write LOD level count
            resource::endian::writeLE<uint32_t>(outFile, resource::LOD_LEVEL_COUNT);

            // Convert Assimp mesh to LODMeshData (LOD0)
            LODMeshData lod0 = convertAssimpMesh(assimpMesh);

            // Generate all LOD levels
            auto lodLevels = generateLODLevels(lod0);

            // Write each LOD level (vertex/index data)
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                writeLODLevel(outFile, lodLevels[lod]);
            }

            // Generate meshlets for each LOD level
            vfLogInfo("  Generating meshlets...");
            std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT> meshletResults;
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                meshletResults[lod] = buildMeshletsForLOD(lodLevels[lod]);
            }

            // Write meshlet data
            writeMeshletData(outFile, meshletResults);

            // Generate and write convex decomposition (v0.0.5+)
            // Use LOD0 for best accuracy in convex decomposition
            resource::ConvexDecompositionData convexData = generateConvexDecomposition(
                lod0, config.meshConfig);
            writeConvexDecompositionData(outFile, convexData);

            // Report progress: 20% + (i+1)/totalMeshes * 80%
            if (progressCallback)
            {
                float progress = 0.2f + (static_cast<float>(i + 1) / scene->mNumMeshes) * 0.8f;
                progressCallback(progress);
            }
        }

        outFile.close();
        vfLogInfo("Mesh with LOD and meshlets saved to: {}", newFileLocation.string());
    }

    resource::ConvexDecompositionData Mesh::generateConvexDecomposition(
        const LODMeshData& meshData,
        const importConfig::MeshImportConfig& config) const
    {
        resource::ConvexDecompositionData result;

        if (!config.generateConvexDecomposition || meshData.vertices.empty() || meshData.indices.empty())
        {
            return result;
        }

        vfLogInfo("  Running V-HACD convex decomposition...");

        // Prepare vertex data (convert to double for V-HACD)
        std::vector<double> points;
        points.reserve(meshData.vertices.size() * 3);
        for (const auto& v : meshData.vertices)
        {
            points.push_back(static_cast<double>(v.position.x));
            points.push_back(static_cast<double>(v.position.y));
            points.push_back(static_cast<double>(v.position.z));
        }

        // Configure V-HACD parameters
        VHACD::IVHACD::Parameters params;
        params.m_maxConvexHulls = config.maxConvexHulls;
        params.m_resolution = config.vhacdResolution;
        params.m_maxNumVerticesPerCH = config.maxVerticesPerHull;
        params.m_minimumVolumePercentErrorAllowed = static_cast<double>(config.minVolumePercentError);
        params.m_maxRecursionDepth = 10;
        params.m_shrinkWrap = true;
        params.m_asyncACD = false;  // Synchronous for import pipeline

        // Create V-HACD instance and compute
        VHACD::IVHACD* vhacd = VHACD::CreateVHACD();

        bool success = vhacd->Compute(
            points.data(),
            static_cast<uint32_t>(meshData.vertices.size()),
            meshData.indices.data(),
            static_cast<uint32_t>(meshData.indices.size() / 3),
            params
        );

        if (success)
        {
            uint32_t numHulls = vhacd->GetNConvexHulls();
            vfLogInfo("  V-HACD generated {} convex hulls", numHulls);

            result.hasDecomposition = true;
            result.params.maxConvexHulls = config.maxConvexHulls;
            result.params.resolution = config.vhacdResolution;
            result.params.maxVerticesPerHull = config.maxVerticesPerHull;
            result.params.minVolumePercentError = config.minVolumePercentError;

            result.hulls.resize(numHulls);

            for (uint32_t i = 0; i < numHulls; ++i)
            {
                VHACD::IVHACD::ConvexHull hull;
                vhacd->GetConvexHull(i, hull);

                auto& outHull = result.hulls[i];
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

            vfLogInfo("  Total convex hull vertices: {}", result.getTotalVertexCount());
        }
        else
        {
            vfLogWarning("  V-HACD decomposition failed");
        }

        vhacd->Release();
        return result;
    }

    void Mesh::writeConvexDecompositionData(std::ofstream& outFile,
                                            const resource::ConvexDecompositionData& decomposition) const
    {
        // Write flag indicating whether decomposition data exists
        resource::endian::writeLE<uint8_t>(outFile, decomposition.hasDecomposition ? 1 : 0);

        if (!decomposition.hasDecomposition)
        {
            return;
        }

        // Write parameters (for reproducibility/debugging)
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxConvexHulls);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.resolution);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxVerticesPerHull);
        resource::endian::writeLE<float>(outFile, decomposition.params.minVolumePercentError);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxRecursionDepth);

        // Write hull count
        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(decomposition.hulls.size()));

        // Write each hull
        for (const auto& hull : decomposition.hulls)
        {
            // Vertex count and vertices
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(hull.vertices.size()));
            for (const auto& v : hull.vertices)
            {
                resource::endian::writeLE<float>(outFile, v.x);
                resource::endian::writeLE<float>(outFile, v.y);
                resource::endian::writeLE<float>(outFile, v.z);
            }

            // Index count and indices (for visualization/debug, not needed for physics)
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(hull.indices.size()));
            for (uint32_t idx : hull.indices)
            {
                resource::endian::writeLE<uint32_t>(outFile, idx);
            }

            // Center and volume
            resource::endian::writeLE<float>(outFile, hull.center.x);
            resource::endian::writeLE<float>(outFile, hull.center.y);
            resource::endian::writeLE<float>(outFile, hull.center.z);
            resource::endian::writeLE<float>(outFile, hull.volume);
        }
    }
}
