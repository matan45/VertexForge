#include "print/Log.hpp"
#include "Mesh.hpp"
#include "Texture.hpp"
#include "MeshLODGenerator.hpp"
#include "MeshSerializer.hpp"
#include "FractureProcessor.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "config/Config.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/VertexQuantization.hpp"

#include <vector>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <cctype>
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

    void writeFileHeader(std::ofstream& outFile, uint32_t numMeshes)
    {
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(resource::FileType::MESH));
        resource::endian::writeLE<uint32_t>(outFile, Version::major);
        resource::endian::writeLE<uint32_t>(outFile, Version::minor);
        resource::endian::writeLE<uint32_t>(outFile, Version::patch);
        resource::endian::writeLE<uint32_t>(outFile, numMeshes);
        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(resource::MeshCompressionFlags::ALL));
    }

    void writeSubmeshHeader(std::ofstream& outFile, const aiMesh* assimpMesh)
    {
        std::string meshName = assimpMesh->mName.C_Str();

        vfLogDebug("Processing submesh '{}' ({} vertices, {} triangles)...",
                  meshName, assimpMesh->mNumVertices, assimpMesh->mNumFaces);

        uint32_t nameLength = static_cast<uint32_t>(meshName.length());
        resource::endian::writeLE<uint32_t>(outFile, nameLength);
        if (nameLength > 0)
        {
            outFile.write(meshName.data(), nameLength);
        }

        resource::endian::writeLE<uint32_t>(outFile, resource::LOD_LEVEL_COUNT);
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
}

namespace types
{
    void Mesh::loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                            std::string_view location, MeshProgressCallback progressCallback,
                            std::vector<std::string>* outWrittenFiles,
                            std::vector<std::string>* outWrittenTextures) const
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

        saveToFileStreamingWithLOD(location, fileName, scene, file.config, progressCallback, outWrittenFiles);

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

        // VK-55: extract textures embedded in the model (gated by the importer
        // option, surfaced here via a non-null out-pointer).
        if (outWrittenTextures)
        {
            extractEmbeddedTextures(scene, location, fileName, file.config, *outWrittenTextures);
        }

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
            writeSubmeshHeader(outFile, scene->mMeshes[i]);

            LODMeshData lod0 = convertAssimpMesh(scene->mMeshes[i], skeleton);
            auto lodLevels = lodGen.generateLODLevels(lod0);

            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                serializer.writeLODLevelCompressed(outFile, lodLevels[lod]);

            vfLogDebug("  Generating meshlets...");
            std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT> meshletResults;
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                meshletResults[lod] = lodGen.buildMeshletsForLOD(lodLevels[lod]);

            serializer.writeMeshletData(outFile, meshletResults);

            resource::ConvexDecompositionData convexData = lodGen.generateConvexDecomposition(lod0, config.meshConfig);
            serializer.writeConvexDecompositionData(outFile, convexData);

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
