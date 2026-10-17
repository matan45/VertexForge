#include "../print/Log.hpp"
#include "AnimationResource.hpp"
#include "EndianUtils.hpp"
#include "VFSHelpers.hpp"

#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace resource
{
    namespace
    {
        // readString()'s 10000-byte cap, except that an over-long or cut-off string fails
        // instead of yielding "" with the stream left mid-string.
        std::optional<std::string> readSummaryString(std::istream& file)
        {
            const uint32_t length = endian::readLE<uint32_t>(file);
            if (!file || length > 10000)
                return std::nullopt;

            std::string str(length, '\0');
            if (length > 0)
                file.read(str.data(), length);
            if (!file)
                return std::nullopt;
            return str;
        }

        // Skips one key array: a uint32 count, then count * keyBytes bytes.
        bool skipKeys(std::istream& file, std::streamoff keyBytes, std::streamoff fileSize)
        {
            const uint32_t count = endian::readLE<uint32_t>(file);
            if (!file)
                return false;

            file.seekg(static_cast<std::streamoff>(count) * keyBytes, std::ios::cur);
            if (!file)
                return false;

            // Seeking past the end of a file stream succeeds; only the next read would fail.
            const auto position = static_cast<std::streamoff>(file.tellg());
            return position >= 0 && position <= fileSize;
        }
    }

    AnimationData AnimationResource::loadAnimation(std::string_view path)
    {
        AnimationData data;

        auto fileData = resource::readFileBytes(std::string(path));
        if (fileData.empty())
        {
            vfLogError("Failed to read animation file: {}", path);
            return data;
        }

        std::string dataStr(fileData.begin(), fileData.end());
        std::istringstream file(dataStr, std::ios::binary);

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

    AnimationClipHeader AnimationResource::readClipSummary(std::string_view path)
    {
        AnimationClipHeader header;
        const std::string truncated = "the file is truncated or corrupt";

        std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            header.error = "the file could not be opened";
            return header;
        }

        const auto fileSize = static_cast<std::streamoff>(file.tellg());
        file.seekg(0, std::ios::beg);
        if (fileSize <= 0 || !file)
        {
            header.error = "the file is empty";
            return header;
        }

        const uint8_t fileType = endian::readLE<uint8_t>(file);
        if (!file || static_cast<FileType>(fileType) != FileType::ANIMATION)
        {
            header.error = "not a .vfAnim file";
            return header;
        }

        const uint32_t major = endian::readLE<uint32_t>(file);
        const uint32_t minor = endian::readLE<uint32_t>(file);
        const uint32_t patch = endian::readLE<uint32_t>(file);
        if (!file)
        {
            header.error = truncated;
            return header;
        }
        if (major != Version::major || minor != Version::minor || patch != Version::patch)
        {
            header.error = std::format("incompatible .vfAnim version {}.{}.{} (expected {}.{}.{}); re-import the source file",
                                       major, minor, patch, Version::major, Version::minor, Version::patch);
            return header;
        }

        std::optional<std::string> name = readSummaryString(file);
        const float durationTicks = endian::readLE<float>(file);
        const float storedTicksPerSecond = endian::readLE<float>(file);
        const uint32_t numChannels = endian::readLE<uint32_t>(file);
        if (!name || !file)
        {
            header.error = truncated;
            return header;
        }
        if (numChannels > 1000)
        {
            header.error = std::format("invalid channel count {}", numChannels);
            return header;
        }

        // Key layout (writer: import/types/Animation.cpp writeChannels): position and scale
        // keys are time + vec3, rotation keys are time + quat.
        constexpr auto vec3KeyBytes = static_cast<std::streamoff>(4 * sizeof(float));
        constexpr auto quatKeyBytes = static_cast<std::streamoff>(5 * sizeof(float));

        std::vector<std::string> channelNames;
        channelNames.reserve(numChannels);
        for (uint32_t c = 0; c < numChannels; ++c)
        {
            std::optional<std::string> boneName = readSummaryString(file);
            if (!boneName ||
                !skipKeys(file, vec3KeyBytes, fileSize) ||
                !skipKeys(file, quatKeyBytes, fileSize) ||
                !skipKeys(file, vec3KeyBytes, fileSize))
            {
                header.error = truncated;
                return header;
            }
            channelNames.push_back(std::move(*boneName));
        }

        // Same acceptance as loadAnimation(): a missing event section or a count of 256+
        // loads no events.
        const uint32_t numEvents = endian::readLE<uint32_t>(file);
        header.eventCount = (file && numEvents < 256) ? numEvents : 0;

        header.name = std::move(*name);
        header.durationTicks = durationTicks;
        header.ticksPerSecond = storedTicksPerSecond > 0.0f ? storedTicksPerSecond : 24.0f;
        header.durationSeconds = durationTicks / header.ticksPerSecond;
        header.channelNames = std::move(channelNames);
        header.success = true;
        return header;
    }

    std::streampos AnimationResource::getEventDataOffset(std::string_view path)
    {
        auto fileData = resource::readFileBytes(std::string(path));
        if (fileData.empty())
        {
            vfLogError("AnimationResource: Failed to read for offset query: {}", path);
            return 0;
        }

        std::string dataStr(fileData.begin(), fileData.end());
        std::istringstream file(dataStr, std::ios::binary);

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

    std::string AnimationResource::readString(std::istream& file)
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

    void AnimationResource::skipString(std::istream& file)
    {
        uint32_t length = endian::readLE<uint32_t>(file);
        if (length > 0 && length <= 10000)
        {
            file.seekg(length, std::ios::cur);
        }
    }
}
