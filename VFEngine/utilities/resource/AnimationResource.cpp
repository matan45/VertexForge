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

        uint8_t fileType = endian::readLE<uint8_t>(file);
        if (static_cast<FileType>(fileType) != FileType::ANIMATION)
        {
            vfLogError("Invalid animation file type: expected {}, got {}",
                       static_cast<int>(FileType::ANIMATION), static_cast<int>(fileType));
            return data;
        }

        data.version.major = endian::readLE<uint32_t>(file);
        data.version.minor = endian::readLE<uint32_t>(file);
        data.version.patch = endian::readLE<uint32_t>(file);

        // v0.0.8+: keyframes only, skeleton comes from mesh
        if (data.version.patch < 8)
        {
            vfLogError("Animation file version {} is not supported. Please re-import your animations.",
                       data.version.patch);
            return data;
        }

        data.name = readString(file);
        data.duration = endian::readLE<float>(file);
        data.ticksPerSecond = endian::readLE<float>(file);

        uint32_t numChannels = endian::readLE<uint32_t>(file);
        if (numChannels > 1000)
        {
            vfLogError("Invalid channel count in animation file: {}", numChannels);
            return data;
        }

        data.channels.resize(numChannels);
        for (auto& channel : data.channels)
        {
            channel.boneName = readString(file);

            uint32_t numPosKeys = endian::readLE<uint32_t>(file);
            channel.positionKeys.resize(numPosKeys);
            for (auto& key : channel.positionKeys)
            {
                key.time = endian::readLE<float>(file);
                key.position.x = endian::readLE<float>(file);
                key.position.y = endian::readLE<float>(file);
                key.position.z = endian::readLE<float>(file);
            }

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

        vfLogInfo("Loaded animation '{}' - {} channels, duration: {:.2f}s",
                  data.name, data.channels.size(), data.duration / data.ticksPerSecond);

        return data;
    }

    std::string AnimationResource::readString(std::ifstream& file)
    {
        uint32_t length = endian::readLE<uint32_t>(file);
        if (length == 0)
        {
            return "";
        }

        if (length > 10000)
        {
            return "";
        }

        std::string str(length, '\0');
        file.read(str.data(), length);
        return str;
    }
}
