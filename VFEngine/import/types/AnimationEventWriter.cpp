#include "AnimationEventWriter.hpp"
#include "resource/EndianUtils.hpp"
#include "print/EditorLogger.hpp"

#include <fstream>
#include <vector>
#include <filesystem>

namespace
{
    std::string readString(std::ifstream& file)
    {
        uint32_t length = resource::endian::readLE<uint32_t>(file);
        if (length == 0 || length > 10000)
        {
            return "";
        }
        std::string str(length, '\0');
        file.read(str.data(), length);
        return str;
    }

    void writeString(std::ofstream& file, const std::string& str)
    {
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(str.size()));
        if (!str.empty())
        {
            file.write(str.data(), str.size());
        }
    }
}

namespace types
{
    std::streampos AnimationEventWriter::findEventDataOffset(const std::string& animPath)
    {
        std::ifstream file(animPath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("AnimationEventWriter: Cannot open file: {}", animPath);
            return 0;
        }

        // Parse header
        uint8_t fileType = resource::endian::readLE<uint8_t>(file);
        uint32_t majorVersion = resource::endian::readLE<uint32_t>(file);
        uint32_t minorVersion = resource::endian::readLE<uint32_t>(file);
        uint32_t patchVersion = resource::endian::readLE<uint32_t>(file);

        if (patchVersion < 8)
        {
            vfLogError("AnimationEventWriter: Unsupported version {}.{}.{}", majorVersion, minorVersion, patchVersion);
            return 0;
        }

        // Skip name
        readString(file);

        // Skip duration + ticksPerSecond
        resource::endian::readLE<float>(file);
        resource::endian::readLE<float>(file);

        // Skip all channels
        uint32_t numChannels = resource::endian::readLE<uint32_t>(file);
        if (numChannels > 1000)
        {
            vfLogError("AnimationEventWriter: Invalid channel count {}", numChannels);
            return 0;
        }

        for (uint32_t c = 0; c < numChannels; ++c)
        {
            // Skip bone name
            readString(file);

            // Skip position keys
            uint32_t numPosKeys = resource::endian::readLE<uint32_t>(file);
            file.seekg(static_cast<std::streamoff>(numPosKeys) * 4 * sizeof(float), std::ios::cur); // time + xyz

            // Skip rotation keys
            uint32_t numRotKeys = resource::endian::readLE<uint32_t>(file);
            file.seekg(static_cast<std::streamoff>(numRotKeys) * 5 * sizeof(float), std::ios::cur); // time + xyzw

            // Skip scale keys
            uint32_t numScaleKeys = resource::endian::readLE<uint32_t>(file);
            file.seekg(static_cast<std::streamoff>(numScaleKeys) * 4 * sizeof(float), std::ios::cur); // time + xyz

            if (file.fail())
            {
                vfLogError("AnimationEventWriter: Failed parsing channel {}", c);
                return 0;
            }
        }

        // This is where event data starts (or should start)
        return file.tellg();
    }

    bool AnimationEventWriter::saveEventsToAnimation(const std::string& animPath,
                                                      const std::vector<animator::AnimationEvent>& events)
    {
        if (!std::filesystem::exists(animPath))
        {
            vfLogError("AnimationEventWriter: File does not exist: {}", animPath);
            return false;
        }

        // Find where event data should be written
        std::streampos eventOffset = findEventDataOffset(animPath);
        if (eventOffset == std::streampos(0))
        {
            return false;
        }

        // Read all file data before the event section
        std::vector<char> prefixData;
        {
            std::ifstream file(animPath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("AnimationEventWriter: Cannot open file for reading: {}", animPath);
                return false;
            }

            prefixData.resize(static_cast<size_t>(eventOffset));
            file.read(prefixData.data(), static_cast<std::streamsize>(eventOffset));

            if (file.fail())
            {
                vfLogError("AnimationEventWriter: Failed to read file prefix");
                return false;
            }
        }

        // Rewrite the file: prefix + new event data
        {
            std::ofstream file(animPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                vfLogError("AnimationEventWriter: Cannot open file for writing: {}", animPath);
                return false;
            }

            // Write everything before events
            file.write(prefixData.data(), static_cast<std::streamsize>(prefixData.size()));

            // Write event data
            uint32_t eventCount = static_cast<uint32_t>(events.size());
            resource::endian::writeLE<uint32_t>(file, eventCount);

            for (const auto& event : events)
            {
                writeString(file, event.name);
                resource::endian::writeLE<float>(file, event.normalizedTime);
                writeString(file, event.payload);
            }

            if (file.fail())
            {
                vfLogError("AnimationEventWriter: Failed to write event data");
                return false;
            }
        }

        vfLogInfo("AnimationEventWriter: Saved {} events to {}", events.size(), animPath);
        return true;
    }
}
