#include "Animation.hpp"
#include "print/EditorLogger.hpp"
#include "resource/EndianUtils.hpp"

#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/anim.h>

#include <glm/gtc/type_ptr.hpp>

namespace
{
    // Assimp to GLM conversion helpers (internal to this translation unit)
    glm::mat4 convertMatrix(const aiMatrix4x4& m)
    {
        // Assimp uses row-major, GLM uses column-major
        // We need to transpose during conversion
        return glm::transpose(glm::make_mat4(&m.a1));
    }

    glm::quat convertQuaternion(const aiQuaternion& q)
    {
        // GLM quaternion constructor: (w, x, y, z)
        return glm::quat(q.w, q.x, q.y, q.z);
    }

    glm::vec3 convertVector(const aiVector3D& v)
    {
        return glm::vec3(v.x, v.y, v.z);
    }
}

namespace types
{
    void Animation::loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                                 std::string_view location,
                                 AnimationProgressCallback progressCallback) const
    {
        if (progressCallback) progressCallback(0.0f);

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(file.path.data(),
                                                 aiProcess_Triangulate |
                                                 aiProcess_LimitBoneWeights);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            vfLogError("Failed to load file for animation extraction: {}", importer.GetErrorString());
            return;
        }

        if (progressCallback) progressCallback(0.1f);

        extractFromScene(scene, fileName, location, progressCallback);

        if (progressCallback) progressCallback(1.0f);
    }

    void Animation::extractFromScene(const aiScene* scene, std::string_view fileName,
                                     std::string_view location,
                                     AnimationProgressCallback progressCallback) const
    {
        if (scene->mNumAnimations == 0)
        {
            vfLogInfo("No animations found in file: {}", fileName);
            return;
        }

        vfLogInfo("Found {} animation(s) in file: {}", scene->mNumAnimations, fileName);

        if (progressCallback) progressCallback(0.15f);

        // Extract skeleton in mesh-compatible bone order (self-contained animation)
        auto skeleton = extractSkeleton(scene);

        // Build bone name to index map
        std::unordered_map<std::string, int32_t> boneIndexMap;
        for (size_t i = 0; i < skeleton.size(); ++i)
            boneIndexMap[skeleton[i].name] = static_cast<int32_t>(i);

        // Extract inverse bind poses from mesh bones (for self-contained animation)
        auto inverseBindPoseMap = extractInverseBindPoses(scene);

        // Extract mesh data (vertices with bone weights)
        std::vector<resource::Vertex> meshVertices;
        std::vector<uint32_t> meshIndices;
        extractMeshData(scene, boneIndexMap, meshVertices, meshIndices);
        vfLogInfo("Extracted mesh: {} vertices, {} indices", meshVertices.size(), meshIndices.size());

        // Compute global inverse transform from scene root
        glm::mat4 rootTransform = convertMatrix(scene->mRootNode->mTransformation);
        glm::mat4 globalInverseTransform = glm::inverse(rootTransform);

        if (progressCallback) progressCallback(0.25f);

        // Process each animation
        const uint32_t numAnimations = scene->mNumAnimations;
        for (uint32_t i = 0; i < numAnimations; ++i)
        {
            const aiAnimation* anim = scene->mAnimations[i];

            // Extract animation data with inverse bind poses (self-contained)
            resource::AnimationData animData = extractAnimation(anim, skeleton, inverseBindPoseMap, globalInverseTransform);

            // Add mesh data to animation (v0.0.7 - fully self-contained)
            animData.vertices = meshVertices;
            animData.indices = meshIndices;

            // Generate output filename
            std::string animName = sanitizeAnimationName(anim->mName.C_Str());
            if (animName.empty())
            {
                animName = "anim_" + std::to_string(i);
            }

            std::string outputName = std::string(fileName) + "_" + animName;

            // Save animation to file
            saveToFile(location, outputName, animData);

            // Report progress
            if (progressCallback)
            {
                float progress = 0.25f + (static_cast<float>(i + 1) / numAnimations) * 0.7f;
                progressCallback(progress);
            }

            vfLogInfo("Exported animation: {} (duration: {:.2f}s, {} channels)",
                      animData.name, animData.duration / animData.ticksPerSecond,
                      animData.channels.size());
        }

        if (progressCallback) progressCallback(0.95f);
    }

    std::vector<resource::SkeletonBone> Animation::extractSkeleton(const aiScene* scene) const
    {
        // Collect bones in the SAME ORDER as mesh import uses
        // This is critical for bone indices to match between mesh vertices and animation matrices
        std::vector<std::string> boneNamesInOrder;
        std::unordered_set<std::string> boneNamesSet;

        for (uint32_t m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh* mesh = scene->mMeshes[m];
            for (uint32_t b = 0; b < mesh->mNumBones; ++b)
            {
                const aiBone* bone = mesh->mBones[b];
                std::string boneName = bone->mName.C_Str();

                // Skip if already added (same logic as mesh import)
                if (boneNamesSet.contains(boneName))
                    continue;

                boneNamesSet.insert(boneName);
                boneNamesInOrder.push_back(boneName);
            }
        }

        if (boneNamesInOrder.empty())
        {
            vfLogWarning("No bones found in meshes - skeleton will be empty");
            return {};
        }

        // Build bone name to index map (matching mesh import order)
        std::unordered_map<std::string, int32_t> boneIndexMap;
        for (size_t i = 0; i < boneNamesInOrder.size(); ++i)
        {
            boneIndexMap[boneNamesInOrder[i]] = static_cast<int32_t>(i);
        }

        // Build node name to node map for hierarchy lookup
        std::unordered_map<std::string, const aiNode*> nodeMap;
        buildNodeMap(scene->mRootNode, nodeMap);

        // Create bones in the same order as mesh import
        std::vector<resource::SkeletonBone> bones;
        bones.reserve(boneNamesInOrder.size());

        for (const auto& boneName : boneNamesInOrder)
        {
            resource::SkeletonBone bone;
            bone.name = boneName;

            // Find this bone's node in the scene hierarchy
            auto nodeIt = nodeMap.find(boneName);
            if (nodeIt != nodeMap.end())
            {
                const aiNode* boneNode = nodeIt->second;

                // Get the bone's local transform
                bone.offsetMatrix = convertMatrix(boneNode->mTransformation);

                // Find parent bone index by walking up the hierarchy
                bone.parentIndex = -1;
                const aiNode* parentNode = boneNode->mParent;

                while (parentNode != nullptr)
                {
                    std::string parentName = parentNode->mName.C_Str();
                    auto parentIt = boneIndexMap.find(parentName);
                    if (parentIt != boneIndexMap.end())
                    {
                        bone.parentIndex = parentIt->second;
                        break;
                    }
                    parentNode = parentNode->mParent;
                }

                // Compute preTransform (accumulated transforms from non-bone ancestors)
                bone.preTransform = glm::mat4(1.0f);
                parentNode = boneNode->mParent;
                std::vector<glm::mat4> nonBoneTransforms;

                while (parentNode != nullptr)
                {
                    std::string parentName = parentNode->mName.C_Str();
                    if (boneIndexMap.find(parentName) != boneIndexMap.end())
                    {
                        // Found a bone parent, stop accumulating
                        break;
                    }
                    // This is a non-bone node, accumulate its transform
                    nonBoneTransforms.push_back(convertMatrix(parentNode->mTransformation));
                    parentNode = parentNode->mParent;
                }

                // Apply non-bone transforms in reverse order (root to leaf)
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

            bones.push_back(bone);
        }

        vfLogInfo("Extracted skeleton with {} bones in mesh-compatible order", bones.size());
        return bones;
    }

    void Animation::buildNodeMap(const aiNode* node, std::unordered_map<std::string, const aiNode*>& nodeMap) const
    {
        nodeMap[node->mName.C_Str()] = node;
        for (uint32_t i = 0; i < node->mNumChildren; ++i)
        {
            buildNodeMap(node->mChildren[i], nodeMap);
        }
    }

    std::unordered_map<std::string, glm::mat4> Animation::extractInverseBindPoses(const aiScene* scene) const
    {
        std::unordered_map<std::string, glm::mat4> inverseBindPoses;

        for (uint32_t m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh* mesh = scene->mMeshes[m];
            for (uint32_t b = 0; b < mesh->mNumBones; ++b)
            {
                const aiBone* bone = mesh->mBones[b];
                std::string boneName = bone->mName.C_Str();

                // Skip if already extracted
                if (inverseBindPoses.contains(boneName))
                    continue;

                // aiBone.mOffsetMatrix IS the inverse bind pose
                glm::mat4 invBindPose = convertMatrix(bone->mOffsetMatrix);
                inverseBindPoses[boneName] = invBindPose;

                // DEBUG: Print first 5 bones
                if (inverseBindPoses.size() <= 5)
                {
                    glm::mat4 bindPose = glm::inverse(invBindPose);
                    vfLogInfo("IMPORT Bone[{}] '{}': invBind[3]=({:.2f},{:.2f},{:.2f}) bindPos=({:.2f},{:.2f},{:.2f})",
                        inverseBindPoses.size() - 1, boneName,
                        invBindPose[3][0], invBindPose[3][1], invBindPose[3][2],
                        bindPose[3][0], bindPose[3][1], bindPose[3][2]);
                }
            }
        }

        return inverseBindPoses;
    }

    resource::AnimationData Animation::extractAnimation(const aiAnimation* anim,
                                                        const std::vector<resource::SkeletonBone>& skeleton,
                                                        const std::unordered_map<std::string, glm::mat4>& inverseBindPoseMap,
                                                        const glm::mat4& globalInverseTransform) const
    {
        resource::AnimationData animData;
        animData.headerFileType = resource::FileType::ANIMATION;
        animData.version = {Version::major, Version::minor, Version::patch};
        animData.name = anim->mName.C_Str();
        animData.duration = static_cast<float>(anim->mDuration);
        animData.ticksPerSecond = anim->mTicksPerSecond > 0.0 ? static_cast<float>(anim->mTicksPerSecond) : 24.0f;
        animData.skeleton = skeleton;
        animData.globalInverseTransform = globalInverseTransform;

        // Build inverse bind poses vector in skeleton bone order
        animData.inverseBindPoses.resize(skeleton.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < skeleton.size(); ++i)
        {
            auto it = inverseBindPoseMap.find(skeleton[i].name);
            if (it != inverseBindPoseMap.end())
            {
                animData.inverseBindPoses[i] = it->second;
            }
            else
            {
                vfLogWarning("No inverse bind pose found for bone '{}', using identity", skeleton[i].name);
            }
        }

        // Extract animation channels, merging FBX helper nodes
        // FBX splits transforms into separate Translation/Rotation/Scaling nodes
        std::unordered_map<std::string, resource::BoneAnimation> channelMap;

        for (uint32_t c = 0; c < anim->mNumChannels; ++c)
        {
            const aiNodeAnim* channel = anim->mChannels[c];
            std::string channelName = channel->mNodeName.C_Str();

            // Strip Assimp's FBX helper suffixes: "Bone_$AssimpFbx$_Rotation" -> "Bone"
            size_t assimpSuffix = channelName.find("_$AssimpFbx$");
            if (assimpSuffix != std::string::npos)
                channelName = channelName.substr(0, assimpSuffix);

            // Get or create channel for this bone
            auto& boneAnim = channelMap[channelName];
            if (boneAnim.boneName.empty())
                boneAnim.boneName = channelName;

            // Merge keyframes (only add if this channel has them)
            if (channel->mNumPositionKeys > 0 && boneAnim.positionKeys.empty())
            {
                for (uint32_t k = 0; k < channel->mNumPositionKeys; ++k)
                {
                    const auto& key = channel->mPositionKeys[k];
                    boneAnim.positionKeys.push_back({static_cast<float>(key.mTime), convertVector(key.mValue)});
                }
            }

            if (channel->mNumRotationKeys > 0 && boneAnim.rotationKeys.empty())
            {
                for (uint32_t k = 0; k < channel->mNumRotationKeys; ++k)
                {
                    const auto& key = channel->mRotationKeys[k];
                    boneAnim.rotationKeys.push_back({static_cast<float>(key.mTime), convertQuaternion(key.mValue)});
                }
            }

            if (channel->mNumScalingKeys > 0 && boneAnim.scalingKeys.empty())
            {
                for (uint32_t k = 0; k < channel->mNumScalingKeys; ++k)
                {
                    const auto& key = channel->mScalingKeys[k];
                    boneAnim.scalingKeys.push_back({static_cast<float>(key.mTime), convertVector(key.mValue)});
                }
            }
        }

        // Convert map to vector
        animData.channels.reserve(channelMap.size());
        for (auto& [name, channel] : channelMap)
            animData.channels.push_back(std::move(channel));

        vfLogInfo("Merged {} raw channels into {} bone channels", anim->mNumChannels, animData.channels.size());

        return animData;
    }

    void Animation::saveToFile(std::string_view location, std::string_view baseName,
                               const resource::AnimationData& animData) const
    {
        // Create output path
        std::filesystem::path outputPath = std::filesystem::path(location) /
                                           (std::string(baseName) + "." + FileExtension::animation);

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile)
        {
            vfLogError("Failed to open file for writing: {}", outputPath.string());
            return;
        }

        // Write header - version 0.0.7: includes mesh data
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(animData.headerFileType));
        resource::endian::writeLE<uint32_t>(outFile, 0);  // major
        resource::endian::writeLE<uint32_t>(outFile, 0);  // minor
        resource::endian::writeLE<uint32_t>(outFile, 7);  // patch - version 0.0.7: adds mesh data

        // Write animation name
        writeString(outFile, animData.name);

        // Write metadata
        resource::endian::writeLE<float>(outFile, animData.duration);
        resource::endian::writeLE<float>(outFile, animData.ticksPerSecond);

        // Write empty skeleton reference (self-contained format)
        writeString(outFile, "");

        // Write inline skeleton (bone hierarchy)
        writeSkeleton(outFile, animData.skeleton);

        // Write inverse bind poses
        writeInverseBindPoses(outFile, animData.inverseBindPoses);

        // Write channels
        writeChannels(outFile, animData.channels);

        // Write global inverse transform (16 floats)
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                resource::endian::writeLE<float>(outFile, animData.globalInverseTransform[col][row]);

        // Write mesh data (v0.0.7)
        writeMeshData(outFile, animData.vertices, animData.indices);

        outFile.close();
        vfLogInfo("Animation saved (v0.0.7): {} bones, {} vertices, {} indices",
                  animData.skeleton.size(), animData.vertices.size(), animData.indices.size());
    }

    void Animation::writeString(std::ofstream& file, const std::string& str) const
    {
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(str.size()));
        if (!str.empty())
        {
            file.write(str.data(), str.size());
        }
    }

    void Animation::writeSkeleton(std::ofstream& file, const std::vector<resource::SkeletonBone>& skeleton) const
    {
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(skeleton.size()));

        for (const auto& bone : skeleton)
        {
            // Write bone name
            writeString(file, bone.name);

            // Write parent index
            resource::endian::writeLE<int32_t>(file, bone.parentIndex);

            // Write offset matrix (16 floats, column-major)
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    resource::endian::writeLE<float>(file, bone.offsetMatrix[col][row]);
                }
            }

            // Write preTransform matrix (16 floats, column-major)
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    resource::endian::writeLE<float>(file, bone.preTransform[col][row]);
                }
            }
        }
    }

    void Animation::writeInverseBindPoses(std::ofstream& file, const std::vector<glm::mat4>& inverseBindPoses) const
    {
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(inverseBindPoses.size()));

        for (const auto& matrix : inverseBindPoses)
        {
            // Write matrix (16 floats, column-major)
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    resource::endian::writeLE<float>(file, matrix[col][row]);
                }
            }
        }
    }

    void Animation::writeChannels(std::ofstream& file, const std::vector<resource::BoneAnimation>& channels) const
    {
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(channels.size()));

        for (const auto& channel : channels)
        {
            // Write bone name
            writeString(file, channel.boneName);

            // Write position keys
            resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(channel.positionKeys.size()));
            for (const auto& key : channel.positionKeys)
            {
                resource::endian::writeLE<float>(file, key.time);
                resource::endian::writeLE<float>(file, key.position.x);
                resource::endian::writeLE<float>(file, key.position.y);
                resource::endian::writeLE<float>(file, key.position.z);
            }

            // Write rotation keys
            resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(channel.rotationKeys.size()));
            for (const auto& key : channel.rotationKeys)
            {
                resource::endian::writeLE<float>(file, key.time);
                resource::endian::writeLE<float>(file, key.rotation.x);
                resource::endian::writeLE<float>(file, key.rotation.y);
                resource::endian::writeLE<float>(file, key.rotation.z);
                resource::endian::writeLE<float>(file, key.rotation.w);
            }

            // Write scaling keys
            resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(channel.scalingKeys.size()));
            for (const auto& key : channel.scalingKeys)
            {
                resource::endian::writeLE<float>(file, key.time);
                resource::endian::writeLE<float>(file, key.scale.x);
                resource::endian::writeLE<float>(file, key.scale.y);
                resource::endian::writeLE<float>(file, key.scale.z);
            }
        }
    }

    std::string Animation::sanitizeAnimationName(const std::string& name)
    {
        std::string result;
        result.reserve(name.size());

        for (char c : name)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
            {
                result += c;
            }
            else if (c == ' ' || c == '|')
            {
                result += '_';
            }
        }

        while (!result.empty() && result.front() == '_')
            result.erase(result.begin());
        while (!result.empty() && result.back() == '_')
            result.pop_back();

        return result;
    }

    void Animation::extractMeshData(const aiScene* scene,
                                    const std::unordered_map<std::string, int32_t>& boneIndexMap,
                                    std::vector<resource::Vertex>& outVertices,
                                    std::vector<uint32_t>& outIndices) const
    {
        outVertices.clear();
        outIndices.clear();

        uint32_t vertexOffset = 0;

        for (uint32_t m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh* mesh = scene->mMeshes[m];

            // Extract vertices
            for (uint32_t v = 0; v < mesh->mNumVertices; ++v)
            {
                resource::Vertex vertex;
                vertex.position = convertVector(mesh->mVertices[v]);
                vertex.normal = mesh->HasNormals() ? convertVector(mesh->mNormals[v]) : glm::vec3(0, 1, 0);
                vertex.texCoords = mesh->HasTextureCoords(0)
                    ? glm::vec2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y)
                    : glm::vec2(0);
                vertex.boneIndices = glm::ivec4(-1);
                vertex.boneWeights = glm::vec4(0);
                outVertices.push_back(vertex);
            }

            // Extract bone weights
            for (uint32_t b = 0; b < mesh->mNumBones; ++b)
            {
                const aiBone* bone = mesh->mBones[b];
                auto it = boneIndexMap.find(bone->mName.C_Str());
                if (it == boneIndexMap.end()) continue;

                int32_t boneIndex = it->second;

                for (uint32_t w = 0; w < bone->mNumWeights; ++w)
                {
                    uint32_t vertIdx = vertexOffset + bone->mWeights[w].mVertexId;
                    float weight = bone->mWeights[w].mWeight;

                    auto& vert = outVertices[vertIdx];
                    for (int i = 0; i < 4; ++i)
                    {
                        if (vert.boneIndices[i] < 0)
                        {
                            vert.boneIndices[i] = boneIndex;
                            vert.boneWeights[i] = weight;
                            break;
                        }
                    }
                }
            }

            // Extract indices
            for (uint32_t f = 0; f < mesh->mNumFaces; ++f)
            {
                const aiFace& face = mesh->mFaces[f];
                for (uint32_t i = 0; i < face.mNumIndices; ++i)
                    outIndices.push_back(vertexOffset + face.mIndices[i]);
            }

            vertexOffset += mesh->mNumVertices;
        }

        // Normalize bone weights
        for (auto& v : outVertices)
        {
            float total = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
            if (total > 0.0f)
                v.boneWeights /= total;
        }
    }

    void Animation::writeMeshData(std::ofstream& file, const std::vector<resource::Vertex>& vertices,
                                  const std::vector<uint32_t>& indices) const
    {
        // Write vertex count and data
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(vertices.size()));
        for (const auto& v : vertices)
        {
            resource::endian::writeLE<float>(file, v.position.x);
            resource::endian::writeLE<float>(file, v.position.y);
            resource::endian::writeLE<float>(file, v.position.z);
            resource::endian::writeLE<float>(file, v.normal.x);
            resource::endian::writeLE<float>(file, v.normal.y);
            resource::endian::writeLE<float>(file, v.normal.z);
            resource::endian::writeLE<float>(file, v.texCoords.x);
            resource::endian::writeLE<float>(file, v.texCoords.y);
            resource::endian::writeLE<int32_t>(file, v.boneIndices.x);
            resource::endian::writeLE<int32_t>(file, v.boneIndices.y);
            resource::endian::writeLE<int32_t>(file, v.boneIndices.z);
            resource::endian::writeLE<int32_t>(file, v.boneIndices.w);
            resource::endian::writeLE<float>(file, v.boneWeights.x);
            resource::endian::writeLE<float>(file, v.boneWeights.y);
            resource::endian::writeLE<float>(file, v.boneWeights.z);
            resource::endian::writeLE<float>(file, v.boneWeights.w);
        }

        // Write index count and data
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(indices.size()));
        for (uint32_t idx : indices)
            resource::endian::writeLE<uint32_t>(file, idx);
    }
}
