#include "Mesh.hpp"
#include "print/EditorLogger.hpp"
#include "resource/EndianUtils.hpp"

#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cfloat>
#include <unordered_set>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <meshoptimizer.h>

#define ENABLE_VHACD_IMPLEMENTATION 1
#include <VHACD.h>

namespace
{
    glm::mat4 convertMatrix(const aiMatrix4x4& m)
    {
        return glm::transpose(glm::mat4(
            m.a1, m.a2, m.a3, m.a4,
            m.b1, m.b2, m.b3, m.b4,
            m.c1, m.c2, m.c3, m.c4,
            m.d1, m.d2, m.d3, m.d4
        ));
    }

    void buildNodeMap(const aiNode* node, std::unordered_map<std::string, const aiNode*>& nodeMap)
    {
        nodeMap[node->mName.C_Str()] = node;
        for (uint32_t i = 0; i < node->mNumChildren; ++i)
        {
            buildNodeMap(node->mChildren[i], nodeMap);
        }
    }
}

namespace types
{
    ExtractedSkeleton Mesh::extractSkeleton(const aiScene* scene) const
    {
        ExtractedSkeleton result;

        std::vector<std::string> boneNamesInOrder;
        std::unordered_set<std::string> boneNamesSet;

        for (uint32_t m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh* mesh = scene->mMeshes[m];
            if (!mesh->HasBones())
                continue;

            result.hasSkinning = true;

            for (uint32_t b = 0; b < mesh->mNumBones; ++b)
            {
                const aiBone* bone = mesh->mBones[b];
                std::string boneName = bone->mName.C_Str();

                if (boneNamesSet.contains(boneName))
                    continue;

                boneNamesSet.insert(boneName);
                boneNamesInOrder.push_back(boneName);

                glm::mat4 invBindPose = convertMatrix(bone->mOffsetMatrix);
                result.inverseBindPoses.push_back(invBindPose);
            }
        }

        if (!result.hasSkinning)
            return result;

        for (size_t i = 0; i < boneNamesInOrder.size(); ++i)
        {
            result.boneNameToIndex[boneNamesInOrder[i]] = static_cast<uint32_t>(i);
        }

        std::unordered_map<std::string, const aiNode*> nodeMap;
        buildNodeMap(scene->mRootNode, nodeMap);

        glm::mat4 rootTransform = convertMatrix(scene->mRootNode->mTransformation);
        result.globalInverseTransform = glm::inverse(rootTransform);

        result.bones.reserve(boneNamesInOrder.size());

        for (const auto& boneName : boneNamesInOrder)
        {
            resource::SkeletonBone bone;
            bone.name = boneName;

            auto nodeIt = nodeMap.find(boneName);
            if (nodeIt != nodeMap.end())
            {
                const aiNode* boneNode = nodeIt->second;
                bone.offsetMatrix = convertMatrix(boneNode->mTransformation);

                bone.parentIndex = -1;
                const aiNode* parentNode = boneNode->mParent;

                while (parentNode != nullptr)
                {
                    std::string parentName = parentNode->mName.C_Str();
                    auto parentIt = result.boneNameToIndex.find(parentName);
                    if (parentIt != result.boneNameToIndex.end())
                    {
                        bone.parentIndex = static_cast<int32_t>(parentIt->second);
                        break;
                    }
                    parentNode = parentNode->mParent;
                }

                bone.preTransform = glm::mat4(1.0f);
                parentNode = boneNode->mParent;
                std::vector<glm::mat4> nonBoneTransforms;

                while (parentNode != nullptr)
                {
                    std::string parentName = parentNode->mName.C_Str();
                    if (result.boneNameToIndex.find(parentName) != result.boneNameToIndex.end())
                        break;

                    nonBoneTransforms.push_back(convertMatrix(parentNode->mTransformation));
                    parentNode = parentNode->mParent;
                }

                for (auto it = nonBoneTransforms.rbegin(); it != nonBoneTransforms.rend(); ++it)
                {
                    bone.preTransform = bone.preTransform * (*it);
                }
            }
            else
            {
                vfLogWarning("Bone '{}' not found in node hierarchy", boneName);
                bone.parentIndex = -1;
                bone.offsetMatrix = glm::mat4(1.0f);
                bone.preTransform = glm::mat4(1.0f);
            }

            result.bones.push_back(bone);
        }

        vfLogInfo("Extracted skeleton with {} bones (full hierarchy)", result.bones.size());
        return result;
    }

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

    LODMeshData Mesh::convertAssimpMesh(const aiMesh* assimpMesh, const ExtractedSkeleton& skeleton) const
    {
        LODMeshData result;
        result.vertices.reserve(assimpMesh->mNumVertices);

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

            vertex.boneIndices = glm::ivec4(-1, -1, -1, -1);
            vertex.boneWeights = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

            result.vertices.push_back(vertex);
        }

        if (assimpMesh->HasBones() && skeleton.hasSkinning)
        {
            std::vector<uint32_t> vertexBoneCount(assimpMesh->mNumVertices, 0);

            for (unsigned int b = 0; b < assimpMesh->mNumBones; ++b)
            {
                const aiBone* bone = assimpMesh->mBones[b];
                std::string boneName = bone->mName.C_Str();

                auto it = skeleton.boneNameToIndex.find(boneName);
                if (it == skeleton.boneNameToIndex.end())
                {
                    vfLogWarning("Bone '{}' not found in skeleton", boneName);
                    continue;
                }

                int32_t boneIndex = static_cast<int32_t>(it->second);

                for (unsigned int w = 0; w < bone->mNumWeights; ++w)
                {
                    uint32_t vertexId = bone->mWeights[w].mVertexId;
                    float weight = bone->mWeights[w].mWeight;

                    if (vertexId >= result.vertices.size())
                        continue;

                    uint32_t& boneSlot = vertexBoneCount[vertexId];
                    if (boneSlot < MAX_BONES_PER_VERTEX)
                    {
                        result.vertices[vertexId].boneIndices[boneSlot] = boneIndex;
                        result.vertices[vertexId].boneWeights[boneSlot] = weight;
                        ++boneSlot;
                    }
                }
            }

            size_t verticesWithBones = 0;
            for (auto& vertex : result.vertices)
            {
                float totalWeight = vertex.boneWeights.x + vertex.boneWeights.y +
                    vertex.boneWeights.z + vertex.boneWeights.w;
                if (totalWeight > 0.0f)
                {
                    vertex.boneWeights /= totalWeight;
                    ++verticesWithBones;
                }
            }

            vfLogInfo("Mesh has {} vertices with bone weights out of {} total",
                      verticesWithBones, result.vertices.size());
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

        if (targetRatio >= 1.0f)
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

        return result;
    }

    std::array<LODMeshData, resource::LOD_LEVEL_COUNT> Mesh::generateLODLevels(const LODMeshData& lod0) const
    {
        std::array<LODMeshData, resource::LOD_LEVEL_COUNT> lodLevels;

        lodLevels[0] = lod0;

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

        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];

            if (m.vertex_count > 255 || m.triangle_count > 255)
            {
                vfLogError("Meshlet {} has invalid counts: vertices={}, triangles={} (max 255)",
                           i, m.vertex_count, m.triangle_count);
                return result;
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
                    return result;
                }

                uint32_t packed =
                    static_cast<uint32_t>(idx0) |
                    (static_cast<uint32_t>(idx1) << 8) |
                    (static_cast<uint32_t>(idx2) << 16);
                result.meshletPrimitives.push_back(packed);
            }
        }

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

        vfLogInfo("    Generated {} meshlets ({} vertex indices, {} primitives)",
                  meshletCount, totalVertexIndices, result.meshletPrimitives.size());

        return result;
    }

    void Mesh::writeMeshletData(std::ofstream& outFile,
                                const std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT>& meshletResults) const
    {
        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            const auto& result = meshletResults[lod];
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshlets.size()));
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshletVertices.size()));
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshletPrimitives.size()));
        }

        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (const auto& meshlet : meshletResults[lod].meshlets)
            {
                resource::endian::writeLE<uint32_t>(outFile, meshlet.descriptor.vertexOffset);
                resource::endian::writeLE<uint32_t>(outFile, meshlet.descriptor.primitiveOffset);
                resource::endian::writeLE<uint8_t>(outFile, meshlet.descriptor.vertexCount);
                resource::endian::writeLE<uint8_t>(outFile, meshlet.descriptor.primitiveCount);
                resource::endian::writeLE<uint16_t>(outFile, 0); // padding

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

        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (uint32_t idx : meshletResults[lod].meshletVertices)
            {
                resource::endian::writeLE<uint32_t>(outFile, idx);
            }
        }

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

        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(lodMesh.vertices.size()));

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
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.x);
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.y);
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.z);
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.w);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.x);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.y);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.z);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.w);
            }
        }

        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(lodMesh.indices.size()));

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

        ExtractedSkeleton skeleton = extractSkeleton(scene);

        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(resource::FileType::MESH));
        resource::endian::writeLE<uint32_t>(outFile, 0);
        resource::endian::writeLE<uint32_t>(outFile, 0);
        resource::endian::writeLE<uint32_t>(outFile, 8);  // v0.0.8 - added cluster DAG support
        resource::endian::writeLE<uint32_t>(outFile, scene->mNumMeshes);

        resource::endian::writeLE<uint32_t>(outFile, 0);

        vfLogInfo("Generating LODs and meshlets for {} submeshes...", scene->mNumMeshes);

        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            const aiMesh* assimpMesh = scene->mMeshes[i];
            std::string meshName = assimpMesh->mName.C_Str();

            vfLogInfo("Processing submesh '{}' ({} vertices, {} triangles)...",
                      meshName,
                      assimpMesh->mNumVertices,
                      assimpMesh->mNumFaces);

            uint32_t nameLength = static_cast<uint32_t>(meshName.length());
            resource::endian::writeLE<uint32_t>(outFile, nameLength);
            if (nameLength > 0)
            {
                outFile.write(meshName.data(), nameLength);
            }

            resource::endian::writeLE<uint32_t>(outFile, resource::LOD_LEVEL_COUNT);

            LODMeshData lod0 = convertAssimpMesh(assimpMesh, skeleton);
            auto lodLevels = generateLODLevels(lod0);

            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                writeLODLevel(outFile, lodLevels[lod]);
            }

            vfLogInfo("  Generating meshlets...");
            std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT> meshletResults;
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                meshletResults[lod] = buildMeshletsForLOD(lodLevels[lod]);
            }

            writeMeshletData(outFile, meshletResults);

            resource::ConvexDecompositionData convexData = generateConvexDecomposition(
                lod0, config.meshConfig);
            writeConvexDecompositionData(outFile, convexData);

            // Write empty cluster DAG placeholder (will be populated by VK-285 cluster builder)
            resource::ClusterDAGData emptyDAG;
            writeClusterDAGData(outFile, emptyDAG);

            if (progressCallback)
            {
                float progress = 0.2f + (static_cast<float>(i + 1) / scene->mNumMeshes) * 0.75f;
                progressCallback(progress);
            }
        }

        writeSkeletonData(outFile, skeleton);

        outFile.close();
        vfLogInfo("Mesh with LOD, meshlets and skeleton reference saved to: {}", newFileLocation.string());
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

        std::vector<double> points;
        points.reserve(meshData.vertices.size() * 3);
        for (const auto& v : meshData.vertices)
        {
            points.push_back(static_cast<double>(v.position.x));
            points.push_back(static_cast<double>(v.position.y));
            points.push_back(static_cast<double>(v.position.z));
        }

        VHACD::IVHACD::Parameters params;
        params.m_maxConvexHulls = config.maxConvexHulls;
        params.m_resolution = config.vhacdResolution;
        params.m_maxNumVerticesPerCH = config.maxVerticesPerHull;
        params.m_minimumVolumePercentErrorAllowed = static_cast<double>(config.minVolumePercentError);
        params.m_maxRecursionDepth = 10;
        params.m_shrinkWrap = true;
        params.m_asyncACD = false; // Synchronous for import pipeline

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

            constexpr uint32_t joltMaxVertices = 256;
            const uint32_t effectiveMaxVertices = std::min(config.maxVerticesPerHull, joltMaxVertices);

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
        resource::endian::writeLE<uint8_t>(outFile, decomposition.hasDecomposition ? 1 : 0);

        if (!decomposition.hasDecomposition)
        {
            return;
        }

        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxConvexHulls);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.resolution);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxVerticesPerHull);
        resource::endian::writeLE<float>(outFile, decomposition.params.minVolumePercentError);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxRecursionDepth);

        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(decomposition.hulls.size()));

        for (const auto& hull : decomposition.hulls)
        {
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(hull.vertices.size()));
            for (const auto& v : hull.vertices)
            {
                resource::endian::writeLE<float>(outFile, v.x);
                resource::endian::writeLE<float>(outFile, v.y);
                resource::endian::writeLE<float>(outFile, v.z);
            }

            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(hull.indices.size()));
            for (uint32_t idx : hull.indices)
            {
                resource::endian::writeLE<uint32_t>(outFile, idx);
            }

            resource::endian::writeLE<float>(outFile, hull.center.x);
            resource::endian::writeLE<float>(outFile, hull.center.y);
            resource::endian::writeLE<float>(outFile, hull.center.z);
            resource::endian::writeLE<float>(outFile, hull.volume);
        }
    }

    void Mesh::writeClusterDAGData(std::ofstream& outFile,
                                   const resource::ClusterDAGData& dagData) const
    {
        bool hasDAG = dagData.header.clusterCount > 0 && !dagData.clusters.empty();
        resource::endian::writeLE<uint8_t>(outFile, hasDAG ? 1 : 0);

        if (!hasDAG)
        {
            return;
        }

        // Write header
        resource::endian::writeLE<uint32_t>(outFile, dagData.header.clusterCount);
        resource::endian::writeLE<uint32_t>(outFile, dagData.header.leafClusterCount);
        resource::endian::writeLE<uint32_t>(outFile, dagData.header.maxDepth);
        resource::endian::writeLE<float>(outFile, dagData.header.maxGeometricError);

        resource::endian::writeLE<float>(outFile, dagData.header.boundingSphere.x);
        resource::endian::writeLE<float>(outFile, dagData.header.boundingSphere.y);
        resource::endian::writeLE<float>(outFile, dagData.header.boundingSphere.z);
        resource::endian::writeLE<float>(outFile, dagData.header.boundingSphere.w);

        // Write clusters (64 bytes each)
        for (const auto& cluster : dagData.clusters)
        {
            // ClusterDescriptor (16 bytes)
            resource::endian::writeLE<uint32_t>(outFile, cluster.descriptor.meshletOffset);
            resource::endian::writeLE<uint16_t>(outFile, cluster.descriptor.meshletCount);
            resource::endian::writeLE<uint16_t>(outFile, cluster.descriptor.triangleCount);
            resource::endian::writeLE<uint32_t>(outFile, cluster.descriptor.vertexOffset);
            resource::endian::writeLE<uint32_t>(outFile, cluster.descriptor.vertexCount);

            // ClusterBounds (32 bytes)
            resource::endian::writeLE<float>(outFile, cluster.bounds.boundingSphere.x);
            resource::endian::writeLE<float>(outFile, cluster.bounds.boundingSphere.y);
            resource::endian::writeLE<float>(outFile, cluster.bounds.boundingSphere.z);
            resource::endian::writeLE<float>(outFile, cluster.bounds.boundingSphere.w);
            resource::endian::writeLE<float>(outFile, cluster.bounds.cone.x);
            resource::endian::writeLE<float>(outFile, cluster.bounds.cone.y);
            resource::endian::writeLE<float>(outFile, cluster.bounds.cone.z);
            resource::endian::writeLE<float>(outFile, cluster.bounds.cone.w);

            // ClusterHierarchy (16 bytes)
            resource::endian::writeLE<uint32_t>(outFile, cluster.hierarchy.parentIndex);
            resource::endian::writeLE<uint32_t>(outFile, cluster.hierarchy.siblingIndex);
            resource::endian::writeLE<float>(outFile, cluster.hierarchy.geometricError);
            resource::endian::writeLE<uint16_t>(outFile, cluster.hierarchy.level);
            resource::endian::writeLE<uint16_t>(outFile, cluster.hierarchy.flags);
        }
    }

    void Mesh::writeSkeletonData(std::ofstream& outFile, const ExtractedSkeleton& skeleton) const
    {
        resource::endian::writeLE<uint8_t>(outFile, skeleton.hasSkinning ? 1 : 0);

        if (!skeleton.hasSkinning)
        {
            return;
        }

        uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
        resource::endian::writeLE<uint32_t>(outFile, boneCount);

        for (const auto& bone : skeleton.bones)
        {
            uint32_t nameLength = static_cast<uint32_t>(bone.name.length());
            resource::endian::writeLE<uint32_t>(outFile, nameLength);
            if (nameLength > 0)
            {
                outFile.write(bone.name.data(), nameLength);
            }

            resource::endian::writeLE<int32_t>(outFile, bone.parentIndex);

            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    resource::endian::writeLE<float>(outFile, bone.offsetMatrix[col][row]);
                }
            }

            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    resource::endian::writeLE<float>(outFile, bone.preTransform[col][row]);
                }
            }
        }

        for (const auto& matrix : skeleton.inverseBindPoses)
        {
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    resource::endian::writeLE<float>(outFile, matrix[col][row]);
                }
            }
        }

        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                resource::endian::writeLE<float>(outFile, skeleton.globalInverseTransform[col][row]);
            }
        }

        vfLogInfo("Written full skeleton data: {} bones with hierarchy", boneCount);
    }
}
