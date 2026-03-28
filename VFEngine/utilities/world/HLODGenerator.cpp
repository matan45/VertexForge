#include "HLODGenerator.hpp"
#include "WorldSectorSerialization.hpp"
#include "../resource/MeshStreamHandle.hpp"
#include "../resource/MeshletTypes.hpp"
#include "../print/Log.hpp"
#include <meshoptimizer.h>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cfloat>

namespace world
{
    using json = nlohmann::json;

    static glm::mat4 buildModelMatrix(const json& transformJson)
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};

        if (transformJson.contains("position"))
        {
            auto& p = transformJson["position"];
            position = {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()};
        }
        if (transformJson.contains("rotation"))
        {
            auto& r = transformJson["rotation"];
            rotation = {glm::radians(r[0].get<float>()), glm::radians(r[1].get<float>()), glm::radians(r[2].get<float>())};
        }
        if (transformJson.contains("scale"))
        {
            auto& s = transformJson["scale"];
            scale = {s[0].get<float>(), s[1].get<float>(), s[2].get<float>()};
        }

        glm::mat4 model(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, rotation.y, glm::vec3(0, 1, 0));
        model = glm::rotate(model, rotation.x, glm::vec3(1, 0, 0));
        model = glm::rotate(model, rotation.z, glm::vec3(0, 0, 1));
        model = glm::scale(model, scale);
        return model;
    }

    bool HLODGenerator::collectSectorGeometry(const std::string& sectorFilePath,
                                               const std::string& workingDirectory,
                                               std::unordered_map<std::string, MergedSubmesh>& materialGroups)
    {
        std::vector<json> entityData;
        if (!WorldSectorSerialization::loadSector(sectorFilePath, entityData))
        {
            vfLogWarning("HLODGenerator: Failed to load sector: {}", sectorFilePath);
            return false;
        }

        for (const auto& entity : entityData)
        {
            if (!entity.contains("components")) continue;
            const auto& components = entity["components"];

            // Need MeshComponent and TransformComponent
            if (!components.contains("MeshComponent") || !components.contains("TransformComponent"))
                continue;

            const auto& meshComp = components["MeshComponent"];
            const auto& transformComp = components["TransformComponent"];

            if (!meshComp.contains("meshRef") || !meshComp["meshRef"].contains("path"))
                continue;

            std::string meshPath = meshComp["meshRef"]["path"].get<std::string>();
            if (meshPath.empty()) continue;

            // Resolve mesh path relative to working directory
            std::string fullMeshPath = workingDirectory + "/" + meshPath;

            // Get transform
            glm::mat4 modelMatrix = buildModelMatrix(transformComp);
            glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));

            // Load mesh LOD0
            resource::MeshStreamHandle meshHandle;
            if (!meshHandle.openStream(fullMeshPath))
            {
                vfLogWarning("HLODGenerator: Failed to open mesh: {}", fullMeshPath);
                continue;
            }

            const auto& meshHeader = meshHandle.getHeader();

            // Determine material per submesh
            std::string defaultMaterial;
            std::unordered_map<std::string, std::string> submeshMaterials;

            if (components.contains("MaterialComponent"))
            {
                const auto& matComp = components["MaterialComponent"];
                if (matComp.contains("defaultMaterialRef") && matComp["defaultMaterialRef"].contains("path"))
                    defaultMaterial = matComp["defaultMaterialRef"]["path"].get<std::string>();

                if (matComp.contains("subMeshMaterials"))
                {
                    for (auto it = matComp["subMeshMaterials"].begin();
                         it != matComp["subMeshMaterials"].end(); ++it)
                    {
                        if (it.value().contains("path"))
                            submeshMaterials[it.key()] = it.value()["path"].get<std::string>();
                    }
                }
            }

            // Read each submesh LOD0 and merge
            for (uint32_t si = 0; si < meshHeader.numSubmeshes; ++si)
            {
                std::vector<resource::Vertex> vertices;
                std::vector<uint32_t> indices;

                if (!meshHandle.readLODLevel(si, 0, vertices, indices))
                    continue;

                if (vertices.empty() || indices.empty())
                    continue;

                // Determine material for this submesh
                std::string material = defaultMaterial;
                const auto& submeshName = meshHeader.submeshes[si].name;
                auto matIt = submeshMaterials.find(submeshName);
                if (matIt != submeshMaterials.end())
                    material = matIt->second;

                if (material.empty())
                    material = "default";

                // Transform vertices to world space
                for (auto& v : vertices)
                {
                    glm::vec4 worldPos = modelMatrix * glm::vec4(v.position, 1.0f);
                    v.position = glm::vec3(worldPos);
                    v.normal = glm::normalize(normalMatrix * v.normal);
                    // Zero out bone data for proxy meshes
                    v.boneIndices = glm::ivec4(-1);
                    v.boneWeights = glm::vec4(0.0f);
                }

                // Merge into material group
                auto& group = materialGroups[material];
                group.materialPath = material;

                uint32_t baseVertex = static_cast<uint32_t>(group.vertices.size());
                group.vertices.insert(group.vertices.end(), vertices.begin(), vertices.end());
                for (uint32_t idx : indices)
                {
                    group.indices.push_back(baseVertex + idx);
                }
            }
        }

        return true;
    }

    void HLODGenerator::simplifySubmesh(std::vector<resource::Vertex>& vertices,
                                         std::vector<uint32_t>& indices,
                                         float targetRatio)
    {
        if (indices.empty() || vertices.empty() || targetRatio >= 1.0f)
            return;

        size_t targetIndexCount = static_cast<size_t>(indices.size() * targetRatio);
        targetIndexCount = std::max(targetIndexCount, static_cast<size_t>(3));
        targetIndexCount = (targetIndexCount / 3) * 3;

        std::vector<uint32_t> simplified(indices.size());
        size_t actualCount = meshopt_simplifySloppy(
            simplified.data(),
            indices.data(),
            indices.size(),
            reinterpret_cast<const float*>(vertices.data()),
            vertices.size(),
            sizeof(resource::Vertex),
            targetIndexCount,
            FLT_MAX,
            nullptr
        );

        simplified.resize(actualCount);

        meshopt_optimizeVertexCache(
            simplified.data(),
            simplified.data(),
            simplified.size(),
            vertices.size()
        );

        // Compact: remove unreferenced vertices
        std::vector<uint32_t> remap(vertices.size(), UINT32_MAX);
        uint32_t newCount = 0;
        for (uint32_t idx : simplified)
        {
            if (remap[idx] == UINT32_MAX)
                remap[idx] = newCount++;
        }

        std::vector<resource::Vertex> compactVerts(newCount);
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            if (remap[i] != UINT32_MAX)
                compactVerts[remap[i]] = vertices[i];
        }

        for (auto& idx : simplified)
            idx = remap[idx];

        vertices = std::move(compactVerts);
        indices = std::move(simplified);
    }

    bool HLODGenerator::buildHLODFileData(const HLODCellCoord& cellCoord,
                                           const HLODTierConfig& tierConfig,
                                           std::unordered_map<std::string, MergedSubmesh>& materialGroups,
                                           HLODFileData& outData)
    {
        static constexpr std::array<float, HLOD_LOD_LEVELS> proxyLODRatios = {1.0f, 0.5f, 0.25f, 0.125f};

        outData = {};
        outData.header.magic = HLOD_MAGIC;
        outData.header.version = HLOD_FORMAT_VERSION;
        outData.header.tier = cellCoord.tier;
        outData.header.cellSize = tierConfig.cellSize;
        outData.header.cellX = cellCoord.x;
        outData.header.cellZ = cellCoord.z;
        outData.header.lodLevelCount = HLOD_LOD_LEVELS;

        glm::vec3 globalMin(FLT_MAX);
        glm::vec3 globalMax(-FLT_MAX);

        uint32_t totalVertexOffset = 0;
        uint32_t totalIndexOffset = 0;
        uint32_t totalMeshletOffset = 0;

        for (auto& [matPath, group] : materialGroups)
        {
            if (group.vertices.empty() || group.indices.empty())
                continue;

            // Simplify the merged group to the tier's target ratio (this becomes proxy LOD0)
            simplifySubmesh(group.vertices, group.indices, tierConfig.simplificationRatio);

            if (group.indices.empty())
                continue;

            HLODSubmeshInfo submeshInfo;
            submeshInfo.materialPath = matPath;

            // Generate 4 LOD levels from the proxy LOD0
            auto proxyLOD0Verts = group.vertices;
            auto proxyLOD0Indices = group.indices;

            for (uint32_t lod = 0; lod < HLOD_LOD_LEVELS; ++lod)
            {
                auto lodVerts = proxyLOD0Verts;
                auto lodIndices = proxyLOD0Indices;

                if (proxyLODRatios[lod] < 1.0f)
                {
                    simplifySubmesh(lodVerts, lodIndices, proxyLODRatios[lod]);
                }

                // Build meshlets for this LOD
                size_t maxMeshlets = meshopt_buildMeshletsBound(
                    lodIndices.size(),
                    resource::MAX_MESHLET_VERTICES,
                    resource::MAX_MESHLET_PRIMITIVES
                );

                std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
                std::vector<unsigned int> meshletVerts(maxMeshlets * resource::MAX_MESHLET_VERTICES);
                std::vector<unsigned char> meshletTris(maxMeshlets * resource::MAX_MESHLET_PRIMITIVES * 3);

                size_t meshletCount = meshopt_buildMeshlets(
                    meshlets.data(),
                    meshletVerts.data(),
                    meshletTris.data(),
                    lodIndices.data(),
                    lodIndices.size(),
                    reinterpret_cast<const float*>(lodVerts.data()),
                    lodVerts.size(),
                    sizeof(resource::Vertex),
                    resource::MAX_MESHLET_VERTICES,
                    resource::MAX_MESHLET_PRIMITIVES,
                    0.0f
                );

                meshlets.resize(meshletCount);

                // Record LOD info
                submeshInfo.lods[lod].vertexOffset = totalVertexOffset;
                submeshInfo.lods[lod].vertexCount = static_cast<uint32_t>(lodVerts.size());
                submeshInfo.lods[lod].indexOffset = totalIndexOffset;
                submeshInfo.lods[lod].indexCount = static_cast<uint32_t>(lodIndices.size());
                submeshInfo.lods[lod].meshletOffset = totalMeshletOffset;
                submeshInfo.lods[lod].meshletCount = static_cast<uint32_t>(meshletCount);

                // Append data
                outData.vertices.insert(outData.vertices.end(), lodVerts.begin(), lodVerts.end());
                outData.indices.insert(outData.indices.end(), lodIndices.begin(), lodIndices.end());

                // Append meshlet data as raw bytes
                size_t meshletBlobBytes = meshletCount * sizeof(meshopt_Meshlet);
                size_t prevSize = outData.meshletData.meshletBlob.size();
                outData.meshletData.meshletBlob.resize(prevSize + meshletBlobBytes);
                memcpy(outData.meshletData.meshletBlob.data() + prevSize, meshlets.data(), meshletBlobBytes);

                // Trim meshlet vertex indices
                size_t totalMeshletVerts = 0;
                for (size_t m = 0; m < meshletCount; ++m)
                    totalMeshletVerts += meshlets[m].vertex_count;

                outData.meshletData.meshletVertices.insert(
                    outData.meshletData.meshletVertices.end(),
                    meshletVerts.begin(), meshletVerts.begin() + totalMeshletVerts);

                // Trim meshlet triangle indices
                size_t totalMeshletTris = 0;
                for (size_t m = 0; m < meshletCount; ++m)
                    totalMeshletTris += meshlets[m].triangle_count * 3;

                outData.meshletData.meshletPrimitives.insert(
                    outData.meshletData.meshletPrimitives.end(),
                    meshletTris.begin(), meshletTris.begin() + totalMeshletTris);

                totalVertexOffset += static_cast<uint32_t>(lodVerts.size());
                totalIndexOffset += static_cast<uint32_t>(lodIndices.size());
                totalMeshletOffset += static_cast<uint32_t>(meshletCount);

                // Update bounds from LOD0 only
                if (lod == 0)
                {
                    for (const auto& v : lodVerts)
                    {
                        globalMin = glm::min(globalMin, v.position);
                        globalMax = glm::max(globalMax, v.position);
                    }
                }
            }

            outData.submeshes.push_back(std::move(submeshInfo));
        }

        outData.header.submeshCount = static_cast<uint32_t>(outData.submeshes.size());
        outData.header.totalVertexCount = totalVertexOffset;
        outData.header.totalIndexCount = totalIndexOffset;
        outData.header.aabbMinX = globalMin.x;
        outData.header.aabbMinY = globalMin.y;
        outData.header.aabbMinZ = globalMin.z;
        outData.header.aabbMaxX = globalMax.x;
        outData.header.aabbMaxY = globalMax.y;
        outData.header.aabbMaxZ = globalMax.z;

        return outData.header.submeshCount > 0;
    }

    bool HLODGenerator::generateForSector(const SectorCoord& coord,
                                           const std::string& sectorFilePath,
                                           const std::string& workingDirectory,
                                           const HLODTierConfig& tierConfig,
                                           const std::string& outputFilePath,
                                           HLODProgressCallback progressCallback)
    {
        auto report = [&](float progress, std::string_view stage)
        {
            if (progressCallback) progressCallback(progress, stage);
            vfLogInfo("HLOD Gen [{},{}] {:.0f}%: {}", coord.x, coord.z, progress * 100.0f, stage);
        };

        report(0.0f, "Collecting geometry");
        std::unordered_map<std::string, MergedSubmesh> materialGroups;
        if (!collectSectorGeometry(sectorFilePath, workingDirectory, materialGroups))
        {
            vfLogWarning("HLODGenerator: No geometry collected for sector [{},{}]", coord.x, coord.z);
            return false;
        }

        if (materialGroups.empty())
        {
            vfLogWarning("HLODGenerator: No mesh entities in sector [{},{}]", coord.x, coord.z);
            return false;
        }

        report(0.3f, "Simplifying and building meshlets");
        HLODFileData fileData;
        HLODCellCoord cellCoord(coord.x, coord.z, tierConfig.tier);

        if (!buildHLODFileData(cellCoord, tierConfig, materialGroups, fileData))
        {
            vfLogError("HLODGenerator: Failed to build HLOD data for sector [{},{}]", coord.x, coord.z);
            return false;
        }

        report(0.8f, "Writing file");
        if (!HLODSerialization::save(outputFilePath, fileData))
        {
            vfLogError("HLODGenerator: Failed to save HLOD file: {}", outputFilePath);
            return false;
        }

        report(1.0f, "Complete");
        vfLogInfo("HLOD generated for sector [{},{}]: {} submeshes, {} vertices, {} indices",
                  coord.x, coord.z, fileData.header.submeshCount,
                  fileData.header.totalVertexCount, fileData.header.totalIndexCount);
        return true;
    }

    bool HLODGenerator::generateForCell(const HLODCellCoord& cellCoord,
                                         const std::vector<std::string>& sectorFilePaths,
                                         const std::string& workingDirectory,
                                         const HLODTierConfig& tierConfig,
                                         const SectorConfig& sectorConfig,
                                         const std::string& outputFilePath,
                                         HLODProgressCallback progressCallback)
    {
        auto report = [&](float progress, std::string_view stage)
        {
            if (progressCallback) progressCallback(progress, stage);
        };

        report(0.0f, "Collecting geometry from sectors");

        std::unordered_map<std::string, MergedSubmesh> materialGroups;
        float step = 0.6f / static_cast<float>(std::max(sectorFilePaths.size(), size_t(1)));
        float current = 0.0f;

        for (const auto& sectorPath : sectorFilePaths)
        {
            collectSectorGeometry(sectorPath, workingDirectory, materialGroups);
            current += step;
            report(current, "Collecting geometry from sectors");
        }

        if (materialGroups.empty())
        {
            vfLogWarning("HLODGenerator: No geometry for cell [{},{},T{}]",
                         cellCoord.x, cellCoord.z, cellCoord.tier);
            return false;
        }

        report(0.6f, "Simplifying and building meshlets");
        HLODFileData fileData;
        if (!buildHLODFileData(cellCoord, tierConfig, materialGroups, fileData))
        {
            return false;
        }

        report(0.9f, "Writing file");
        if (!HLODSerialization::save(outputFilePath, fileData))
        {
            return false;
        }

        report(1.0f, "Complete");
        vfLogInfo("HLOD generated for cell [{},{},T{}]: {} submeshes, {} vertices",
                  cellCoord.x, cellCoord.z, cellCoord.tier,
                  fileData.header.submeshCount, fileData.header.totalVertexCount);
        return true;
    }

} // namespace world
