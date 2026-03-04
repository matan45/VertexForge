#include "../print/Log.hpp"
#include "AnimationResource.hpp"
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

        if (data.version.major != Version::major || data.version.minor != Version::minor || data.version.patch != Version::patch)
        {
            vfLogError("Incompatible animation file version: {}.{}.{}, expected {}.{}.{}. Re-import required.",
                       data.version.major, data.version.minor, data.version.patch,
                       Version::major, Version::minor, Version::patch);
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

        // Read animation events (mandatory at v1.0)
        uint32_t numEvents = endian::readLE<uint32_t>(file);
        if (!file.fail() && numEvents > 0 && numEvents < 256)
        {
            data.events.resize(numEvents);
            for (uint32_t e = 0; e < numEvents; ++e)
            {
                auto& event = data.events[e];
                event.name = readString(file);
                event.normalizedTime = endian::readLE<float>(file);
                event.payload = readString(file);
            }
        }

        vfLogInfo("Loaded animation '{}' - {} channels, duration: {:.2f}s",
                  data.name, data.channels.size(), data.duration / data.ticksPerSecond);

        return data;
    }

    std::streampos AnimationResource::getEventDataOffset(std::string_view path)
    {
        std::ifstream file(path.data(), std::ios::binary);
        if (!file)
        {
            vfLogError("AnimationResource: Failed to open for offset query: {}", path);
            return 0;
        }

        uint8_t fileType = endian::readLE<uint8_t>(file);
        if (static_cast<FileType>(fileType) != FileType::ANIMATION)
            return 0;

        uint32_t major = endian::readLE<uint32_t>(file);
        uint32_t minor = endian::readLE<uint32_t>(file);
        uint32_t patch = endian::readLE<uint32_t>(file);

        if (major != Version::major || minor != Version::minor || patch != Version::patch)
            return 0;

        // Skip name, duration, ticksPerSecond
        skipString(file);
        endian::readLE<float>(file);
        endian::readLE<float>(file);

        // Skip all channels
        uint32_t numChannels = endian::readLE<uint32_t>(file);
        if (numChannels > 1000)
            return 0;

        for (uint32_t c = 0; c < numChannels; ++c)
        {
            skipString(file); // bone name

            uint32_t numPosKeys = endian::readLE<uint32_t>(file);
            file.seekg(static_cast<std::streamoff>(numPosKeys) * 4 * sizeof(float), std::ios::cur);

            uint32_t numRotKeys = endian::readLE<uint32_t>(file);
            file.seekg(static_cast<std::streamoff>(numRotKeys) * 5 * sizeof(float), std::ios::cur);

            uint32_t numScaleKeys = endian::readLE<uint32_t>(file);
            file.seekg(static_cast<std::streamoff>(numScaleKeys) * 4 * sizeof(float), std::ios::cur);

            if (file.fail())
                return 0;
        }

        return file.tellg();
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

    void AnimationResource::skipString(std::ifstream& file)
    {
        uint32_t length = endian::readLE<uint32_t>(file);
        if (length > 0 && length <= 10000)
        {
            file.seekg(length, std::ios::cur);
        }
    }
}
