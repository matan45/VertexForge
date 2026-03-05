#include "print/Log.hpp"
#include "Mesh.hpp"
#include "MeshLODGenerator.hpp"
#include "MeshSerializer.hpp"
#include "config/Config.hpp"
#include "resource/EndianUtils.hpp"

#include <vector>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

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

    void collectBoneData(const aiScene* scene, types::ExtractedSkeleton& result,
                         std::vector<std::string>& boneNamesInOrder)
    {
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

        for (size_t i = 0; i < boneNamesInOrder.size(); ++i)
        {
            result.boneNameToIndex[boneNamesInOrder[i]] = static_cast<uint32_t>(i);
        }
    }

    void buildBoneHierarchy(const aiScene* scene, types::ExtractedSkeleton& result,
                            const std::vector<std::string>& boneNamesInOrder)
    {
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
    }

    // Reorder bones so that every parent index < child index (topological order).
    // This is required by AnimationEvaluator and IKPostProcessor which iterate
    // bones in index order and read parent transforms that must already be computed.
    void ensureParentBeforeChildOrder(types::ExtractedSkeleton& skeleton)
    {
        const size_t boneCount = skeleton.bones.size();
        if (boneCount <= 1)
            return;

        // Build old-index order via BFS/queue from roots
        std::vector<uint32_t> order;
        order.reserve(boneCount);

        // Build children list
        std::vector<std::vector<uint32_t>> children(boneCount);
        for (uint32_t i = 0; i < boneCount; ++i)
        {
            int32_t parent = skeleton.bones[i].parentIndex;
            if (parent >= 0 && parent < static_cast<int32_t>(boneCount))
                children[parent].push_back(i);
            else
                order.push_back(i);
        }

        for (size_t head = 0; head < order.size(); ++head)
        {
            for (uint32_t child : children[order[head]])
                order.push_back(child);
        }

        if (order.size() != boneCount)
            return;

        // Check if already in order
        bool alreadySorted = true;
        for (size_t i = 0; i < boneCount; ++i)
        {
            if (order[i] != i) { alreadySorted = false; break; }
        }
        if (alreadySorted)
            return;

        // Build old-to-new index mapping
        std::vector<int32_t> oldToNew(boneCount, -1);
        for (uint32_t newIdx = 0; newIdx < boneCount; ++newIdx)
            oldToNew[order[newIdx]] = static_cast<int32_t>(newIdx);

        // Reorder bones and inverseBindPoses
        std::vector<resource::SkeletonBone> sortedBones(boneCount);
        std::vector<glm::mat4> sortedInvBindPoses(boneCount);

        for (uint32_t newIdx = 0; newIdx < boneCount; ++newIdx)
        {
            uint32_t oldIdx = order[newIdx];
            sortedBones[newIdx] = skeleton.bones[oldIdx];
            sortedInvBindPoses[newIdx] = skeleton.inverseBindPoses[oldIdx];

            int32_t oldParent = sortedBones[newIdx].parentIndex;
            sortedBones[newIdx].parentIndex = (oldParent >= 0) ? oldToNew[oldParent] : -1;
        }

        skeleton.bones = std::move(sortedBones);
        skeleton.inverseBindPoses = std::move(sortedInvBindPoses);

        // Rebuild name-to-index map
        for (auto& [name, idx] : skeleton.boneNameToIndex)
            idx = static_cast<uint32_t>(oldToNew[idx]);
    }

    void extractVertices(const aiMesh* assimpMesh, types::LODMeshData& result)
    {
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
    }

    void assignBoneWeights(const aiMesh* assimpMesh, const types::ExtractedSkeleton& skeleton,
                           types::LODMeshData& result, uint32_t maxBonesPerVertex)
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
                if (boneSlot < maxBonesPerVertex)
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

    void extractIndices(const aiMesh* assimpMesh, types::LODMeshData& result)
    {
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
    }

    void writeFileHeader(std::ofstream& outFile, uint32_t numMeshes)
    {
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(resource::FileType::MESH));
        resource::endian::writeLE<uint32_t>(outFile, Version::major);
        resource::endian::writeLE<uint32_t>(outFile, Version::minor);
        resource::endian::writeLE<uint32_t>(outFile, Version::patch);
        resource::endian::writeLE<uint32_t>(outFile, numMeshes);
        resource::endian::writeLE<uint32_t>(outFile, 0);
    }

    void writeSubmeshHeader(std::ofstream& outFile, const aiMesh* assimpMesh)
    {
        std::string meshName = assimpMesh->mName.C_Str();

        vfLogInfo("Processing submesh '{}' ({} vertices, {} triangles)...",
                  meshName, assimpMesh->mNumVertices, assimpMesh->mNumFaces);

        uint32_t nameLength = static_cast<uint32_t>(meshName.length());
        resource::endian::writeLE<uint32_t>(outFile, nameLength);
        if (nameLength > 0)
        {
            outFile.write(meshName.data(), nameLength);
        }

        resource::endian::writeLE<uint32_t>(outFile, resource::LOD_LEVEL_COUNT);
    }
}

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

    LODMeshData Mesh::convertAssimpMesh(const aiMesh* assimpMesh, const ExtractedSkeleton& skeleton) const
    {
        LODMeshData result;

        extractVertices(assimpMesh, result);

        if (assimpMesh->HasBones() && skeleton.hasSkinning)
        {
            assignBoneWeights(assimpMesh, skeleton, result, MAX_BONES_PER_VERTEX);
        }

        extractIndices(assimpMesh, result);

        return result;
    }

    ExtractedSkeleton Mesh::extractSkeleton(const aiScene* scene) const
    {
        ExtractedSkeleton result;
        std::vector<std::string> boneNamesInOrder;

        collectBoneData(scene, result, boneNamesInOrder);

        if (!result.hasSkinning)
            return result;

        buildBoneHierarchy(scene, result, boneNamesInOrder);
        ensureParentBeforeChildOrder(result);

        vfLogInfo("Extracted skeleton with {} bones (full hierarchy)", result.bones.size());
        return result;
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

        writeFileHeader(outFile, scene->mNumMeshes);

        vfLogInfo("Generating LODs and meshlets for {} submeshes...", scene->mNumMeshes);

        MeshLODGenerator lodGen;
        MeshSerializer serializer;

        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            writeSubmeshHeader(outFile, scene->mMeshes[i]);

            LODMeshData lod0 = convertAssimpMesh(scene->mMeshes[i], skeleton);
            auto lodLevels = lodGen.generateLODLevels(lod0);

            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                serializer.writeLODLevel(outFile, lodLevels[lod]);

            vfLogInfo("  Generating meshlets...");
            std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT> meshletResults;
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                meshletResults[lod] = lodGen.buildMeshletsForLOD(lodLevels[lod]);

            serializer.writeMeshletData(outFile, meshletResults);

            resource::ConvexDecompositionData convexData = lodGen.generateConvexDecomposition(lod0, config.meshConfig);
            serializer.writeConvexDecompositionData(outFile, convexData);

            if (progressCallback)
            {
                float progress = 0.2f + (static_cast<float>(i + 1) / scene->mNumMeshes) * 0.75f;
                progressCallback(progress);
            }
        }

        MeshSerializer{}.writeSkeletonData(outFile, skeleton);

        outFile.close();
        vfLogInfo("Mesh with LOD, meshlets and skeleton reference saved to: {}", newFileLocation.string());
    }
}
