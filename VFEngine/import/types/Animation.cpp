#include "Animation.hpp"
#include "print/EditorLogger.hpp"
#include "resource/EndianUtils.hpp"

#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/anim.h>

#include <glm/gtc/type_ptr.hpp>

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

        if (progressCallback) progressCallback(0.25f);

        const uint32_t numAnimations = scene->mNumAnimations;
        for (uint32_t i = 0; i < numAnimations; ++i)
        {
            const aiAnimation* anim = scene->mAnimations[i];

            resource::AnimationData animData = extractAnimation(anim);

            std::string animName = sanitizeAnimationName(anim->mName.C_Str());
            if (animName.empty())
            {
                animName = "anim_" + std::to_string(i);
            }

            std::string outputName = std::string(fileName) + "_" + animName;

            saveToFile(location, outputName, animData);

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

    resource::AnimationData Animation::extractAnimation(const aiAnimation* anim) const
    {
        resource::AnimationData animData;
        animData.headerFileType = resource::FileType::ANIMATION;
        animData.version = {Version::major, Version::minor, Version::patch};
        animData.name = anim->mName.C_Str();
        animData.duration = static_cast<float>(anim->mDuration);
        animData.ticksPerSecond = anim->mTicksPerSecond > 0.0 ? static_cast<float>(anim->mTicksPerSecond) : 24.0f;

        std::unordered_map<std::string, resource::BoneAnimation> channelMap;

        for (uint32_t c = 0; c < anim->mNumChannels; ++c)
        {
            const aiNodeAnim* channel = anim->mChannels[c];
            std::string channelName = channel->mNodeName.C_Str();

            size_t assimpSuffix = channelName.find("_$AssimpFbx$");
            if (assimpSuffix != std::string::npos)
                channelName = channelName.substr(0, assimpSuffix);

            auto& boneAnim = channelMap[channelName];
            if (boneAnim.boneName.empty())
                boneAnim.boneName = channelName;

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

        animData.channels.reserve(channelMap.size());
        for (auto& [name, channel] : channelMap)
            animData.channels.push_back(std::move(channel));

        vfLogInfo("Extracted {} bone channels", animData.channels.size());

        return animData;
    }

    void Animation::saveToFile(std::string_view location, std::string_view baseName,
                               const resource::AnimationData& animData) const
    {
        std::filesystem::path outputPath = std::filesystem::path(location) /
                                           (std::string(baseName) + "." + FileExtension::animation);

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile)
        {
            vfLogError("Failed to open file for writing: {}", outputPath.string());
            return;
        }

        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(animData.headerFileType));
        resource::endian::writeLE<uint32_t>(outFile, 0);
        resource::endian::writeLE<uint32_t>(outFile, 0);
        resource::endian::writeLE<uint32_t>(outFile, 8);

        writeString(outFile, animData.name);
        resource::endian::writeLE<float>(outFile, animData.duration);
        resource::endian::writeLE<float>(outFile, animData.ticksPerSecond);
        writeChannels(outFile, animData.channels);

        outFile.close();
        vfLogInfo("Animation saved (v0.0.8): {} channels", animData.channels.size());
    }

    void Animation::writeString(std::ofstream& file, const std::string& str) const
    {
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(str.size()));
        if (!str.empty())
        {
            file.write(str.data(), str.size());
        }
    }

    void Animation::writeChannels(std::ofstream& file, const std::vector<resource::BoneAnimation>& channels) const
    {
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(channels.size()));

        for (const auto& channel : channels)
        {
            writeString(file, channel.boneName);

            resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(channel.positionKeys.size()));
            for (const auto& key : channel.positionKeys)
            {
                resource::endian::writeLE<float>(file, key.time);
                resource::endian::writeLE<float>(file, key.position.x);
                resource::endian::writeLE<float>(file, key.position.y);
                resource::endian::writeLE<float>(file, key.position.z);
            }

            resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(channel.rotationKeys.size()));
            for (const auto& key : channel.rotationKeys)
            {
                resource::endian::writeLE<float>(file, key.time);
                resource::endian::writeLE<float>(file, key.rotation.x);
                resource::endian::writeLE<float>(file, key.rotation.y);
                resource::endian::writeLE<float>(file, key.rotation.z);
                resource::endian::writeLE<float>(file, key.rotation.w);
            }

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

    glm::mat4 Animation::convertMatrix(const aiMatrix4x4& m)
    {
        return glm::transpose(glm::make_mat4(&m.a1));
    }

    glm::quat Animation::convertQuaternion(const aiQuaternion& q)
    {
        return glm::quat(q.w, q.x, q.y, q.z);
    }

    glm::vec3 Animation::convertVector(const aiVector3D& v)
    {
        return glm::vec3(v.x, v.y, v.z);
    }
}
