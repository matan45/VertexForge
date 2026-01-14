#include "AnimationResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>

namespace resource
{
    AnimationData AnimationResource::loadAnimation(std::string_view path)
    {
        AnimationData data;

        std::ifstream file(path.data(), std::ios::binary);
        if (!file)
        {
            vfLogError("Failed to open animation file: {}", path);
            return data;
        }

        // Read and validate header
        uint8_t fileType = endian::readLE<uint8_t>(file);
        if (static_cast<FileType>(fileType) != FileType::ANIMATION)
        {
            vfLogError("Invalid animation file type: expected {}, got {}",
                       static_cast<int>(FileType::ANIMATION), static_cast<int>(fileType));
            return data;
        }

        // Read version
        data.version.major = endian::readLE<uint32_t>(file);
        data.version.minor = endian::readLE<uint32_t>(file);
        data.version.patch = endian::readLE<uint32_t>(file);

        // Read animation name
        data.name = readString(file);

        // Read metadata
        data.duration = endian::readLE<float>(file);
        data.ticksPerSecond = endian::readLE<float>(file);

        // Read skeleton
        uint32_t numBones = endian::readLE<uint32_t>(file);
        if (numBones > 1000)  // Sanity check
        {
            vfLogError("Invalid bone count in animation file: {}", numBones);
            return data;
        }

        data.skeleton.resize(numBones);
        for (auto& bone : data.skeleton)
        {
            bone.name = readString(file);
            bone.parentIndex = endian::readLE<int32_t>(file);

            // Read offset matrix (16 floats, column-major)
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    bone.offsetMatrix[col][row] = endian::readLE<float>(file);
                }
            }
        }

        // Read channels
        uint32_t numChannels = endian::readLE<uint32_t>(file);
        if (numChannels > 1000)  // Sanity check
        {
            vfLogError("Invalid channel count in animation file: {}", numChannels);
            return data;
        }

        data.channels.resize(numChannels);
        for (auto& channel : data.channels)
        {
            channel.boneName = readString(file);

            // Read position keys
            uint32_t numPosKeys = endian::readLE<uint32_t>(file);
            channel.positionKeys.resize(numPosKeys);
            for (auto& key : channel.positionKeys)
            {
                key.time = endian::readLE<float>(file);
                key.position.x = endian::readLE<float>(file);
                key.position.y = endian::readLE<float>(file);
                key.position.z = endian::readLE<float>(file);
            }

            // Read rotation keys
            uint32_t numRotKeys = endian::readLE<uint32_t>(file);
            channel.rotationKeys.resize(numRotKeys);
            for (auto& key : channel.rotationKeys)
            {
                key.time = endian::readLE<float>(file);
                key.rotation.x = endian::readLE<float>(file);
                key.rotation.y = endian::readLE<float>(file);
                key.rotation.z = endian::readLE<float>(file);
                key.rotation.w = endian::readLE<float>(file);
            }

            // Read scaling keys
            uint32_t numScaleKeys = endian::readLE<uint32_t>(file);
            channel.scalingKeys.resize(numScaleKeys);
            for (auto& key : channel.scalingKeys)
            {
                key.time = endian::readLE<float>(file);
                key.scale.x = endian::readLE<float>(file);
                key.scale.y = endian::readLE<float>(file);
                key.scale.z = endian::readLE<float>(file);
            }
        }

        data.headerFileType = FileType::ANIMATION;

        vfLogInfo("Loaded animation '{}' - {} bones, {} channels, duration: {:.2f}s",
                  data.name, data.skeleton.size(), data.channels.size(),
                  data.duration / data.ticksPerSecond);

        return data;
    }

    bool AnimationResource::validateFile(std::string_view path)
    {
        std::ifstream file(path.data(), std::ios::binary);
        if (!file)
        {
            return false;
        }

        uint8_t fileType = endian::readLE<uint8_t>(file);
        return static_cast<FileType>(fileType) == FileType::ANIMATION;
    }

    std::string AnimationResource::readString(std::ifstream& file)
    {
        uint32_t length = endian::readLE<uint32_t>(file);
        if (length == 0)
        {
            return "";
        }

        if (length > 10000)  // Sanity check for string length
        {
            return "";
        }

        std::string str(length, '\0');
        file.read(str.data(), length);
        return str;
    }
}
