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

        // Extract skeleton from scene
        auto skeleton = extractSkeleton(scene);

        if (progressCallback) progressCallback(0.25f);

        // Process each animation
        const uint32_t numAnimations = scene->mNumAnimations;
        for (uint32_t i = 0; i < numAnimations; ++i)
        {
            const aiAnimation* anim = scene->mAnimations[i];

            // Extract animation data
            resource::AnimationData animData = extractAnimation(anim, skeleton);

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
        // Collect all bone names and offset matrices from meshes
        std::unordered_map<std::string, glm::mat4> boneOffsets;
        std::unordered_set<std::string> boneNames;

        for (uint32_t m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh* mesh = scene->mMeshes[m];
            for (uint32_t b = 0; b < mesh->mNumBones; ++b)
            {
                const aiBone* bone = mesh->mBones[b];
                std::string boneName = bone->mName.C_Str();
                boneNames.insert(boneName);
                boneOffsets[boneName] = convertMatrix(bone->mOffsetMatrix);
            }
        }

        if (boneNames.empty())
        {
            vfLogWarning("No bones found in meshes - skeleton will be empty");
            return {};
        }

        // Build skeleton hierarchy by traversing the node tree
        std::vector<resource::SkeletonBone> bones;
        std::unordered_map<std::string, int32_t> boneIndexMap;

        buildBoneHierarchy(scene->mRootNode, boneOffsets, boneNames, bones, boneIndexMap, -1);

        return bones;
    }

    void Animation::buildBoneHierarchy(const aiNode* node,
                                       const std::unordered_map<std::string, glm::mat4>& boneOffsets,
                                       const std::unordered_set<std::string>& boneNames,
                                       std::vector<resource::SkeletonBone>& bones,
                                       std::unordered_map<std::string, int32_t>& boneIndexMap,
                                       int32_t parentIndex) const
    {
        std::string nodeName = node->mName.C_Str();
        int32_t currentIndex = parentIndex;

        // Check if this node is a bone
        if (boneNames.find(nodeName) != boneNames.end())
        {
            resource::SkeletonBone bone;
            bone.name = nodeName;
            bone.parentIndex = parentIndex;

            // Get offset matrix if available, otherwise use identity
            auto it = boneOffsets.find(nodeName);
            if (it != boneOffsets.end())
            {
                bone.offsetMatrix = it->second;
            }
            else
            {
                bone.offsetMatrix = glm::mat4(1.0f);
            }

            currentIndex = static_cast<int32_t>(bones.size());
            boneIndexMap[nodeName] = currentIndex;
            bones.push_back(bone);
        }

        // Recursively process children
        for (uint32_t i = 0; i < node->mNumChildren; ++i)
        {
            buildBoneHierarchy(node->mChildren[i], boneOffsets, boneNames, bones, boneIndexMap, currentIndex);
        }
    }

    resource::AnimationData Animation::extractAnimation(const aiAnimation* anim,
                                                        const std::vector<resource::SkeletonBone>& skeleton) const
    {
        resource::AnimationData animData;
        animData.headerFileType = resource::FileType::ANIMATION;
        animData.version = {Version::major, Version::minor, Version::patch};
        animData.name = anim->mName.C_Str();
        animData.duration = static_cast<float>(anim->mDuration);
        animData.ticksPerSecond = anim->mTicksPerSecond > 0.0 ? static_cast<float>(anim->mTicksPerSecond) : 24.0f;
        animData.skeleton = skeleton;

        // Extract animation channels
        animData.channels.reserve(anim->mNumChannels);

        for (uint32_t c = 0; c < anim->mNumChannels; ++c)
        {
            const aiNodeAnim* channel = anim->mChannels[c];

            resource::BoneAnimation boneAnim;
            boneAnim.boneName = channel->mNodeName.C_Str();

            // Extract position keys
            boneAnim.positionKeys.reserve(channel->mNumPositionKeys);
            for (uint32_t k = 0; k < channel->mNumPositionKeys; ++k)
            {
                const aiVectorKey& key = channel->mPositionKeys[k];
                resource::PositionKey posKey;
                posKey.time = static_cast<float>(key.mTime);
                posKey.position = convertVector(key.mValue);
                boneAnim.positionKeys.push_back(posKey);
            }

            // Extract rotation keys
            boneAnim.rotationKeys.reserve(channel->mNumRotationKeys);
            for (uint32_t k = 0; k < channel->mNumRotationKeys; ++k)
            {
                const aiQuatKey& key = channel->mRotationKeys[k];
                resource::RotationKey rotKey;
                rotKey.time = static_cast<float>(key.mTime);
                rotKey.rotation = convertQuaternion(key.mValue);
                boneAnim.rotationKeys.push_back(rotKey);
            }

            // Extract scaling keys
            boneAnim.scalingKeys.reserve(channel->mNumScalingKeys);
            for (uint32_t k = 0; k < channel->mNumScalingKeys; ++k)
            {
                const aiVectorKey& key = channel->mScalingKeys[k];
                resource::ScaleKey scaleKey;
                scaleKey.time = static_cast<float>(key.mTime);
                scaleKey.scale = convertVector(key.mValue);
                boneAnim.scalingKeys.push_back(scaleKey);
            }

            animData.channels.push_back(std::move(boneAnim));
        }

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

        // Write header
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(animData.headerFileType));
        resource::endian::writeLE<uint32_t>(outFile, Version::major);
        resource::endian::writeLE<uint32_t>(outFile, Version::minor);
        resource::endian::writeLE<uint32_t>(outFile, Version::patch);

        // Write animation name
        writeString(outFile, animData.name);

        // Write metadata
        resource::endian::writeLE<float>(outFile, animData.duration);
        resource::endian::writeLE<float>(outFile, animData.ticksPerSecond);

        // Write skeleton
        writeSkeleton(outFile, animData.skeleton);

        // Write channels
        writeChannels(outFile, animData.channels);

        outFile.close();
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
            // Skip other characters
        }

        // Remove leading/trailing underscores
        while (!result.empty() && result.front() == '_')
        {
            result.erase(result.begin());
        }
        while (!result.empty() && result.back() == '_')
        {
            result.pop_back();
        }

        return result;
    }
}
