#include "print/Log.hpp"
#include "Mesh.hpp"
#include "Texture.hpp"
#include "MeshTextureImport.hpp"
#include "MeshLODGenerator.hpp"
#include "MeshSerializer.hpp"
#include "MeshFileLayout.hpp"
#include "FractureProcessor.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "config/Config.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/VertexQuantization.hpp"

#include "cpumem/CpuMemoryManager.hpp"
#include "cpumem/CpuMemoryCategories.hpp"
#include "cpumem/ScopedCpuMemory.hpp"

#include <semaphore>
#include <vector>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <map>
#include <variant>
#include <iterator>
#include <cctype>
#include <cmath>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
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

        vfLogDebug("Mesh has {} vertices with bone weights out of {} total",
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

    // The header layout itself now lives in MeshFileLayout.hpp so ProceduralMeshWriter (VK-1621)
    // writes byte-identical files. These stay as thin forwarders to keep the call sites unchanged.
    void writeFileHeader(std::ofstream& outFile, uint32_t numMeshes)
    {
        types::meshlayout::writeFileHeader(outFile, numMeshes);
    }

    void writeSubmeshHeader(std::ofstream& outFile, std::string_view meshName,
                            size_t vertexCount, size_t triangleCount)
    {
        types::meshlayout::writeSubmeshHeader(outFile, meshName, vertexCount, triangleCount);
    }

    // Strips characters illegal in Windows file names and trims awkward
    // leading/trailing spaces and dots. Returns an empty string if nothing
    // usable remains.
    std::string sanitizeFileStem(std::string_view name)
    {
        std::string out;
        out.reserve(name.size());
        for (char c : name)
        {
            const auto uc = static_cast<unsigned char>(c);
            if (uc < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' ||
                c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
                out.push_back('_');
            else
                out.push_back(c);
        }

        const size_t start = out.find_first_not_of(" .");
        if (start == std::string::npos)
            return {};
        const size_t end = out.find_last_not_of(" .");
        return out.substr(start, end - start + 1);
    }

    std::string toLowerCopy(std::string s)
    {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Reads a bool import option by key from ImportConfig::customOptions (missing or
    // non-bool => false). Lets the mesh saver gate texture extraction on the specific
    // option rather than on the shared out-pointer being non-null.
    bool readBoolOption(const importConfig::ImportConfig& config, std::string_view key)
    {
        const auto it = config.customOptions.find(std::string(key));
        return it != config.customOptions.end() && std::holds_alternative<bool>(it->second) &&
               std::get<bool>(it->second);
    }

    // Per-mesh output stem. A single-mesh model keeps the bare file name
    // (preserves existing references); multi-mesh models append the (sanitized)
    // mesh name, falling back to the index when empty, and dedup collisions
    // case-insensitively so no two files share a path on Windows.
    std::string makeUniqueMeshFileStem(std::string_view baseName, const aiMesh* mesh,
                                       unsigned int index, unsigned int totalMeshes,
                                       std::unordered_set<std::string>& usedLower)
    {
        if (totalMeshes <= 1)
            return std::string(baseName);

        const std::string meshName = sanitizeFileStem(mesh->mName.C_Str());
        const std::string candidate = meshName.empty()
                                          ? std::string(baseName) + "_" + std::to_string(index)
                                          : std::string(baseName) + "_" + meshName;

        std::string unique = candidate;
        unsigned int counter = 0;
        while (!usedLower.insert(toLowerCopy(unique)).second)
            unique = candidate + "_" + std::to_string(counter++);

        return unique;
    }

    struct StaticMeshInstance
    {
        const aiMesh* mesh = nullptr;
        glm::mat4 transform{1.0f};
        std::string nodeName;
    };

    // Combine is a STATIC-mesh feature: it bakes each node's aiNode::mTransformation
    // (the default/bind pose) into world space. A scene-level animation stack (e.g.
    // Bistro's wind props) leaves that default pose intact and is irrelevant to the
    // bake, matching UE5's static-mesh import. Only genuinely skinned meshes (HasBones)
    // must fall back to split output.
    bool isStaticScene(const aiScene* scene)
    {
        if (!scene)
            return false;

        for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
        {
            if (scene->mMeshes[i] && scene->mMeshes[i]->HasBones())
                return false;
        }

        return true;
    }

    void collectStaticMeshInstances(const aiScene* scene, const aiNode* node,
                                    const glm::mat4& parentTransform,
                                    std::vector<StaticMeshInstance>& instances,
                                    std::vector<bool>& referencedMeshes)
    {
        if (!scene || !node)
            return;

        const glm::mat4 nodeTransform = parentTransform * convertMatrix(node->mTransformation);
        for (uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
            const uint32_t meshIndex = node->mMeshes[i];
            if (meshIndex >= scene->mNumMeshes || !scene->mMeshes[meshIndex])
            {
                vfLogWarning("Node '{}' references invalid mesh index {}", node->mName.C_Str(), meshIndex);
                continue;
            }

            instances.push_back({scene->mMeshes[meshIndex], nodeTransform, node->mName.C_Str()});
            referencedMeshes[meshIndex] = true;
        }

        for (uint32_t i = 0; i < node->mNumChildren; ++i)
            collectStaticMeshInstances(scene, node->mChildren[i], nodeTransform, instances, referencedMeshes);
    }

    std::vector<StaticMeshInstance> buildStaticMeshInstances(const aiScene* scene)
    {
        std::vector<StaticMeshInstance> instances;
        if (!scene || !scene->mRootNode)
            return instances;

        instances.reserve(scene->mNumMeshes);
        std::vector<bool> referencedMeshes(scene->mNumMeshes, false);
        collectStaticMeshInstances(scene, scene->mRootNode, glm::mat4(1.0f),
                                   instances, referencedMeshes);

        // Assimp scenes normally reference every mesh from a node. Preserve any
        // malformed-but-readable unreferenced meshes instead of silently dropping them.
        for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
        {
            if (!referencedMeshes[i] && scene->mMeshes[i])
            {
                vfLogWarning("Mesh '{}' is not referenced by the scene hierarchy; importing with identity transform",
                             scene->mMeshes[i]->mName.C_Str());
                instances.push_back({scene->mMeshes[i], glm::mat4(1.0f), {}});
            }
        }

        return instances;
    }

    void applyStaticTransform(types::LODMeshData& meshData, const glm::mat4& transform)
    {
        const glm::mat3 linearTransform(transform);
        const float determinant = glm::determinant(linearTransform);
        const bool invertible = std::abs(determinant) > 1.0e-8f;
        const glm::mat3 normalTransform = invertible
                                              ? glm::transpose(glm::inverse(linearTransform))
                                              : linearTransform;

        if (!invertible)
            vfLogWarning("Combined mesh instance has a singular node transform; degenerate normals may be produced");

        for (auto& vertex : meshData.vertices)
        {
            vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));

            const glm::vec3 transformedNormal = normalTransform * vertex.normal;
            const float normalLengthSquared = glm::dot(transformedNormal, transformedNormal);
            vertex.normal = normalLengthSquared > 1.0e-12f
                                ? transformedNormal / std::sqrt(normalLengthSquared)
                                : glm::vec3(0.0f);
        }

        if (determinant < 0.0f)
        {
            for (size_t i = 0; i + 2 < meshData.indices.size(); i += 3)
                std::swap(meshData.indices[i + 1], meshData.indices[i + 2]);
        }
    }

    // --- Combined per-material section helpers (VK-1641 phase C) --------------
    // Mirror the reader's per-submesh, per-LOD caps (MeshStreamHandle.hpp:62-64,
    // which are private and cannot be referenced across the DLL boundary). LOD0 is
    // the largest LOD, so bounding it bounds every generated LOD.
    constexpr uint64_t kMaxSectionVertexCount = 10'000'000;
    constexpr uint64_t kMaxSectionIndexCount = 30'000'000;
    constexpr size_t kMaxCombinedSections = 10'000; // == MeshStreamHandle maxSubmeshCount

    // Section name = the sanitized material name. Assimp's ScenePreprocessor injects a
    // "DefaultMaterial" for material-less scenes, so the Material_<idx> fallback is
    // effectively unreachable for real scenes but kept for null-safety.
    std::string materialSectionName(const aiScene* scene, unsigned int materialIndex)
    {
        std::string name;
        if (scene && scene->mMaterials && materialIndex < scene->mNumMaterials &&
            scene->mMaterials[materialIndex])
        {
            aiString aiName;
            if (scene->mMaterials[materialIndex]->Get(AI_MATKEY_NAME, aiName) == AI_SUCCESS)
                name = sanitizeFileStem(aiName.C_Str());
        }
        if (name.empty())
            name = "Material_" + std::to_string(materialIndex);
        return name;
    }

    // Case-insensitive unique section name (Windows-path parity with the mesh-file
    // stems). Oversized material groups split naturally into "<mat>", "<mat>_1", ...
    std::string makeUniqueSectionName(const std::string& candidate,
                                      std::unordered_set<std::string>& usedLower)
    {
        std::string unique = candidate;
        unsigned int counter = 1;
        while (!usedLower.insert(toLowerCopy(unique)).second)
            unique = candidate + "_" + std::to_string(counter++);
        return unique;
    }

    // Append an already-transformed instance into a material group's buffers, rebasing
    // its (local, 0-based) indices by the group's current vertex count. Mirrors
    // HLODGenerator::collectSectorGeometry's baseVertex rebase; kept local so Import
    // does not link utilities/world.
    void appendTransformedInstance(types::LODMeshData& group, types::LODMeshData part)
    {
        const uint32_t baseVertex = static_cast<uint32_t>(group.vertices.size());
        group.vertices.insert(group.vertices.end(),
                              std::make_move_iterator(part.vertices.begin()),
                              std::make_move_iterator(part.vertices.end()));
        group.indices.reserve(group.indices.size() + part.indices.size());
        for (uint32_t idx : part.indices)
            group.indices.push_back(baseVertex + idx);
    }

    struct MaterialSection
    {
        unsigned int materialIndex = 0;
        std::vector<size_t> instanceIndices;
    };

    // Pass 1: group instances by material index (ascending => deterministic output),
    // splitting a material into multiple sections when the merged vertex/index totals
    // would exceed the reader caps. Merging never dedups vertices, so summing
    // mNumVertices / face-index counts is EXACT and reproduces the same flush
    // boundaries pass 2 hits — the header section count therefore cannot drift. An
    // instance too large to fit even alone is logged and skipped (dropped from both
    // passes), matching the existing overflow handling.
    std::vector<MaterialSection> buildSectionPlan(const std::vector<StaticMeshInstance>& instances,
                                                  std::string_view fileName)
    {
        std::map<unsigned int, std::vector<size_t>> byMaterial;
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const unsigned int materialIndex = instances[i].mesh ? instances[i].mesh->mMaterialIndex : 0u;
            byMaterial[materialIndex].push_back(i);
        }

        std::vector<MaterialSection> plan;
        for (const auto& [materialIndex, list] : byMaterial)
        {
            MaterialSection current{materialIndex, {}};
            uint64_t currentVerts = 0;
            uint64_t currentIndices = 0;

            for (size_t inst : list)
            {
                const aiMesh* mesh = instances[inst].mesh;
                if (!mesh)
                    continue;

                const uint64_t verts = mesh->mNumVertices;
                uint64_t indices = 0;
                for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
                    indices += mesh->mFaces[f].mNumIndices;

                if (verts > kMaxSectionVertexCount || indices > kMaxSectionIndexCount)
                {
                    vfLogError("Combined mesh '{}': instance '{}' ({} verts / {} indices) exceeds the "
                               "per-submesh limits ({} / {}); skipping",
                               fileName, instances[inst].nodeName, verts, indices,
                               kMaxSectionVertexCount, kMaxSectionIndexCount);
                    continue;
                }

                const bool overflow = !current.instanceIndices.empty() &&
                    (currentVerts + verts > kMaxSectionVertexCount ||
                     currentIndices + indices > kMaxSectionIndexCount);
                if (overflow)
                {
                    plan.push_back(std::move(current));
                    current = MaterialSection{materialIndex, {}};
                    currentVerts = 0;
                    currentIndices = 0;
                }

                current.instanceIndices.push_back(inst);
                currentVerts += verts;
                currentIndices += indices;
            }

            if (!current.instanceIndices.empty())
                plan.push_back(std::move(current));
        }

        return plan;
    }

    // generateConvexData=false writes an empty ConvexDecompositionData (a single 0 byte
    // the reader accepts) and skips V-HACD. Combined static meshes bucket many
    // instances per material, so a scene-wide convex decomposition is meaningless and
    // slow; collision for merged scenes is deferred.
    void writeProcessedSubmesh(std::ofstream& outFile, std::string_view name,
                               types::LODMeshData lod0,
                               const importConfig::MeshImportConfig& config,
                               const types::MeshLODGenerator& lodGen,
                               const types::MeshSerializer& serializer,
                               bool generateConvexData = true)
    {
        writeSubmeshHeader(outFile, name, lod0.vertices.size(), lod0.indices.size() / 3);
        auto lodLevels = lodGen.generateLODLevels(lod0);

        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            serializer.writeLODLevelCompressed(outFile, lodLevels[lod]);

        vfLogDebug("  Generating meshlets...");
        std::array<types::MeshletBuildResult, resource::LOD_LEVEL_COUNT> meshletResults;
        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            meshletResults[lod] = lodGen.buildMeshletsForLOD(lodLevels[lod]);
        serializer.writeMeshletData(outFile, meshletResults);

        const resource::ConvexDecompositionData convexData =
            generateConvexData ? lodGen.generateConvexDecomposition(lod0, config)
                               : resource::ConvexDecompositionData{};
        serializer.writeConvexDecompositionData(outFile, convexData);
    }

    // Mesh decode semaphore: separate from the texture/HDR semaphore in Texture.cpp.
    // Both live in Import.dll but in different TUs, so they are independent permit
    // pools (2+2 max concurrent). Move to a shared Import TU if a unified cap
    // across all decode types is needed.
    constexpr int kMeshDecodeConcurrency = 2;
    std::counting_semaphore<kMeshDecodeConcurrency> gMeshDecodeSem{kMeshDecodeConcurrency};

    struct MeshDecodeLock
    {
        MeshDecodeLock()  { gMeshDecodeSem.acquire(); }
        ~MeshDecodeLock() { gMeshDecodeSem.release(); }
        MeshDecodeLock(const MeshDecodeLock&) = delete;
        MeshDecodeLock& operator=(const MeshDecodeLock&) = delete;
    };

    memory::CategoryId meshDecodeCategory()
    {
        static const memory::CategoryId id =
            memory::CpuMemoryManager::instance().registerCategory(
                memory::categories::ImportMeshDecode, memory::CategoryKind::Transient);
        return id;
    }

}

namespace types
{
    void Mesh::loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                            std::string_view location, MeshOutputLayout outputLayout,
                            MeshProgressCallback progressCallback,
                            std::vector<std::string>* outWrittenFiles,
                            std::vector<std::string>* outWrittenTextures) const
    {
        if (progressCallback) progressCallback(0.0f);

        // Acquire the concurrency slot BEFORE ReadFile so the cap bounds the actual
        // Assimp parse+triangulate allocation. file_size × 4 is a conservative
        // pre-decode upper bound (mesh data expands on triangulation + normal calc).
        std::error_code sizeEc;
        const auto rawMeshBytes = std::filesystem::file_size(file.path.data(), sizeEc);
        const uint64_t decodeEstimate = (!sizeEc && rawMeshBytes > 0)
            ? static_cast<uint64_t>(rawMeshBytes) * 4u
            : 0u;
        MeshDecodeLock decodeLock;
        memory::ScopedCpuMemory decodeGuard(meshDecodeCategory(), decodeEstimate);

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

        if (outputLayout == MeshOutputLayout::CombinedStatic && isStaticScene(scene))
        {
            saveCombinedStaticMesh(location, fileName, scene, file.config, progressCallback, outWrittenFiles);
        }
        else
        {
            if (outputLayout == MeshOutputLayout::CombinedStatic)
            {
                vfLogWarning("Combine Meshes requested for skinned model '{}'; using split mesh output",
                             fileName);
            }
            saveToFileStreamingWithLOD(location, fileName, scene, file.config, progressCallback, outWrittenFiles);
        }

        if (file.config.meshConfig.fractureConfig.generateFractureData)
        {
            try
            {
                generateAndSaveFracturedMesh(location, fileName, scene, file.config, progressCallback);
            }
            catch (const std::exception& e)
            {
                vfLogError("Fracture generation crashed for {}: {}", fileName, e.what());
            }
        }

        // Texture extraction — both write into the shared out-pointer, which the
        // importer makes non-null when either option is on. Each is gated on its own
        // import option (not merely on the pointer):
        //   - VK-55: textures embedded in the model file (aiScene->mTextures).
        //   - VK-1641: textures referenced by the scene's materials, resolved relative
        //     to the source model (or *N embedded). Materials themselves are not created.
        if (outWrittenTextures)
        {
            if (readBoolOption(file.config, "extractEmbeddedTextures"))
                extractEmbeddedTextures(scene, location, fileName, file.config, *outWrittenTextures);

            if (readBoolOption(file.config, "importMaterialTextures"))
                importMaterialTextures(scene, std::filesystem::path(file.path).parent_path(),
                                       location, fileName, file.config, *outWrittenTextures);
        }

        // decodeGuard and decodeLock release here; then importer RAII frees aiScene.
        if (progressCallback) progressCallback(1.0f);
    }

    void Mesh::extractEmbeddedTextures(const aiScene* scene, std::string_view location,
                                       std::string_view fileName, const importConfig::ImportConfig& config,
                                       std::vector<std::string>& outWrittenTextures) const
    {
        if (!scene || scene->mNumTextures == 0)
            return;

        vfLogDebug("Extracting {} embedded texture(s) from '{}'", scene->mNumTextures, fileName);

        Texture textureWriter;
        std::unordered_set<std::string> usedStems;

        for (unsigned int i = 0; i < scene->mNumTextures; ++i)
        {
            const aiTexture* tex = scene->mTextures[i];
            if (!tex || !tex->pcData)
                continue;

            std::string texName = sanitizeFileStem(tex->mFilename.C_Str());
            if (texName.empty())
                texName = "tex" + std::to_string(i);

            const std::string candidate = std::string(fileName) + "_" + texName;
            std::string stem = candidate;
            unsigned int counter = 0;
            while (!usedStems.insert(toLowerCopy(stem)).second)
                stem = candidate + "_" + std::to_string(counter++);

            // Assimp: mHeight == 0 => pcData is a compressed file blob of mWidth
            // bytes; otherwise pcData is mWidth*mHeight uncompressed BGRA texels.
            const bool compressed = tex->mHeight == 0;
            const size_t byteLength = compressed
                                          ? static_cast<size_t>(tex->mWidth)
                                          : static_cast<size_t>(tex->mWidth) * tex->mHeight * 4;

            if (textureWriter.saveEmbeddedTexture(stem, location,
                                                  reinterpret_cast<const unsigned char*>(tex->pcData),
                                                  byteLength, compressed, tex->mWidth, tex->mHeight, config))
            {
                const std::filesystem::path outPath =
                    std::filesystem::path(location) / (stem + "." + FileExtension::textrue);
                outWrittenTextures.push_back(outPath.string());
            }
        }
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

        vfLogDebug("Extracted skeleton with {} bones (full hierarchy)", result.bones.size());
        return result;
    }

    void Mesh::saveToFileStreamingWithLOD(std::string_view location, std::string_view fileName,
                                          const aiScene* scene, const importConfig::ImportConfig& config,
                                          MeshProgressCallback progressCallback,
                                          std::vector<std::string>* outWrittenFiles) const
    {
        if (scene->mNumMeshes == 0)
        {
            vfLogWarning("No meshes found in '{}'; nothing to write", fileName);
            return;
        }

        // The skeleton is scene-wide; extract once and write a copy into each
        // per-mesh file so every .vfMesh stays self-contained.
        ExtractedSkeleton skeleton = extractSkeleton(scene);

        vfLogDebug("Generating LODs and meshlets for {} mesh(es) (one .vfMesh each)...", scene->mNumMeshes);

        MeshLODGenerator lodGen;
        MeshSerializer serializer;

        std::unordered_set<std::string> usedStems;

        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            const std::string stem = makeUniqueMeshFileStem(fileName, scene->mMeshes[i], i,
                                                            scene->mNumMeshes, usedStems);
            const std::filesystem::path newFileLocation =
                std::filesystem::path(location) / (stem + "." + FileExtension::mesh);

            std::ofstream outFile(newFileLocation, std::ios::binary);
            if (!outFile)
            {
                vfLogError("Failed to open file for writing: {}", newFileLocation.string());
                continue;
            }

            // One mesh per file => numMeshes == 1.
            writeFileHeader(outFile, 1);
            LODMeshData lod0 = convertAssimpMesh(scene->mMeshes[i], skeleton);
            writeProcessedSubmesh(outFile, scene->mMeshes[i]->mName.C_Str(), std::move(lod0),
                                  config.meshConfig, lodGen, serializer);

            serializer.writeSkeletonData(outFile, skeleton);

            outFile.close();

            if (outWrittenFiles)
                outWrittenFiles->push_back(newFileLocation.string());

            vfLogDebug("Mesh with LOD, meshlets and skeleton saved to: {}", newFileLocation.string());

            if (progressCallback)
            {
                float progress = 0.2f + (static_cast<float>(i + 1) / scene->mNumMeshes) * 0.75f;
                progressCallback(progress);
            }
        }
    }

    void Mesh::saveCombinedStaticMesh(std::string_view location, std::string_view fileName,
                                      const aiScene* scene, const importConfig::ImportConfig& config,
                                      MeshProgressCallback progressCallback,
                                      std::vector<std::string>* outWrittenFiles) const
    {
        auto instances = buildStaticMeshInstances(scene);
        if (instances.empty())
        {
            vfLogWarning("No mesh instances found in '{}'; nothing to write", fileName);
            return;
        }

        // One section per material (UE5 parity): Bistro's ~1,296 instances collapse to
        // ~132 material sections / draw calls. buildSectionPlan also splits any material
        // whose merged geometry would exceed the reader's per-submesh caps.
        const std::vector<MaterialSection> plan = buildSectionPlan(instances, fileName);
        if (plan.empty())
        {
            vfLogWarning("No writable mesh sections in '{}'; nothing to write", fileName);
            return;
        }
        if (plan.size() > kMaxCombinedSections)
        {
            vfLogError("Combined mesh '{}' has {} sections, exceeding the supported limit of {}",
                       fileName, plan.size(), kMaxCombinedSections);
            return;
        }

        const std::filesystem::path outputPath =
            std::filesystem::path(location) / (std::string(fileName) + "." + FileExtension::mesh);
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile)
        {
            vfLogError("Failed to open file for writing: {}", outputPath.string());
            return;
        }

        writeFileHeader(outFile, static_cast<uint32_t>(plan.size()));

        MeshLODGenerator lodGen;
        MeshSerializer serializer;
        ExtractedSkeleton emptySkeleton;
        std::unordered_set<std::string> usedLower;
        usedLower.reserve(plan.size());

        vfLogDebug("Merging {} instance(s) into {} material section(s) for '{}'...",
                   instances.size(), plan.size(), fileName);
        for (size_t s = 0; s < plan.size(); ++s)
        {
            // Merge every instance in this section into one vertex/index buffer. Order:
            // convert -> bake node transform (per-instance winding swap on the
            // instance's LOCAL indices) -> rebase into the group buffer. Swapping before
            // the baseVertex offset keeps the corrected winding intact.
            LODMeshData group;
            for (size_t inst : plan[s].instanceIndices)
            {
                const auto& instance = instances[inst];
                LODMeshData part = convertAssimpMesh(instance.mesh, emptySkeleton);
                applyStaticTransform(part, instance.transform);
                appendTransformedInstance(group, std::move(part));
            }

            const std::string name =
                makeUniqueSectionName(materialSectionName(scene, plan[s].materialIndex), usedLower);
            writeProcessedSubmesh(outFile, name, std::move(group),
                                  config.meshConfig, lodGen, serializer,
                                  /*generateConvexData=*/false);

            if (progressCallback)
            {
                const float progress = 0.2f +
                    (static_cast<float>(s + 1) / static_cast<float>(plan.size())) * 0.75f;
                progressCallback(progress);
            }
        }

        serializer.writeSkeletonData(outFile, emptySkeleton);
        outFile.close();
        if (!outFile)
        {
            vfLogError("Failed while writing combined mesh: {}", outputPath.string());
            return;
        }

        if (outWrittenFiles)
            outWrittenFiles->push_back(outputPath.string());

        vfLogInfo("Combined mesh saved to: {} ({} material section(s))", outputPath.string(), plan.size());
    }

    void Mesh::generateAndSaveFracturedMesh(std::string_view location, std::string_view fileName,
                                             const aiScene* scene, const importConfig::ImportConfig& config,
                                             MeshProgressCallback progressCallback) const
    {
        if (!scene || scene->mNumMeshes == 0)
        {
            return;
        }

        vfLogInfo("Generating fracture data for mesh: {}", fileName);

        // Build LOD0 mesh data from first submesh for fracture input
        ExtractedSkeleton skeleton = extractSkeleton(scene);
        // NOTE: Only fractures the first submesh. Multi-submesh merging is a future enhancement.
        LODMeshData lod0 = convertAssimpMesh(scene->mMeshes[0], skeleton);

        if (lod0.vertices.size() < 4 || lod0.indices.size() < 12)
        {
            return;
        }

        resource::MeshData inputMesh;
        inputMesh.name = std::string(fileName);
        resource::LODLevel lodLevel;
        lodLevel.vertices = lod0.vertices;
        lodLevel.indices = lod0.indices;
        inputMesh.lodLevels.push_back(std::move(lodLevel));

        // Run fracture processor
        auto fractureResult = FractureProcessor::process(
            inputMesh,
            config.meshConfig.fractureConfig,
            [&](float progress, std::string_view stage)
            {
                if (progressCallback)
                {
                    progressCallback(0.85f + progress * 0.1f);
                }
            });

        if (!fractureResult.success)
        {
            vfLogWarning("Fracture generation failed for {}: {}", fileName, fractureResult.errorMessage);
            return;
        }

        // Write single _fractured.vfMesh with all fragments as submeshes
        std::filesystem::path fracturedPath = std::filesystem::path(location) /
            (std::string(fileName) + "_fractured." + FileExtension::mesh);

        std::ofstream outFile(fracturedPath, std::ios::binary);
        if (!outFile.is_open())
        {
            vfLogError("Failed to open fractured mesh file: {}", fracturedPath.string());
            return;
        }

        MeshSerializer serializer;
        MeshLODGenerator lodGen;
        uint32_t numFragments = static_cast<uint32_t>(fractureResult.fragmentMeshes.meshes.size());
        writeFileHeader(outFile, numFragments);

        for (uint32_t i = 0; i < numFragments; ++i)
        {
            const auto& fragMesh = fractureResult.fragmentMeshes.meshes[i];
            if (fragMesh.lodLevels.empty() || fragMesh.lodLevels[0].vertices.size() < 3 ||
                fragMesh.lodLevels[0].indices.size() < 3)
                continue;

            std::string fragName = fragMesh.name.empty() ? "fragment_" + std::to_string(i) : fragMesh.name;
            uint32_t nameLen = static_cast<uint32_t>(fragName.size());
            resource::endian::writeLE<uint32_t>(outFile, nameLen);
            if (nameLen > 0) outFile.write(fragName.data(), nameLen);
            resource::endian::writeLE<uint32_t>(outFile, resource::LOD_LEVEL_COUNT);

            LODMeshData fragLod0;
            fragLod0.vertices = fragMesh.lodLevels[0].vertices;
            fragLod0.indices = fragMesh.lodLevels[0].indices;
            auto lodLevels = lodGen.generateLODLevels(fragLod0);

            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                serializer.writeLODLevelCompressed(outFile, lodLevels[lod]);

            std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT> meshletResults;
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                meshletResults[lod] = lodGen.buildMeshletsForLOD(lodLevels[lod]);
            serializer.writeMeshletData(outFile, meshletResults);

            const auto& convexData = (i < fractureResult.fragmentConvexHulls.size())
                ? fractureResult.fragmentConvexHulls[i]
                : resource::ConvexDecompositionData{};
            serializer.writeConvexDecompositionData(outFile, convexData);
        }

        ExtractedSkeleton emptySkeleton;
        serializer.writeSkeletonData(outFile, emptySkeleton);
        outFile.close();

        asset::AssetMetadata meta;
        meta.type = resource::AssetType::Mesh;
        meta.fractureData = fractureResult.metadata;
        auto metaPath = asset::AssetMetadataSerializer::getMetaPath(fracturedPath);
        asset::AssetMetadataSerializer::save(meta, metaPath);

        vfLogInfo("Fractured mesh saved: {} ({} fragments)", fracturedPath.string(), numFragments);
    }
}
