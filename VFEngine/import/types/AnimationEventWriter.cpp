#include "AnimationEventWriter.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/AnimationResource.hpp"
#include "print/EditorLogger.hpp"

#include <fstream>
#include <vector>
#include <filesystem>

namespace
{
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
    bool AnimationEventWriter::saveEventsToAnimation(const std::string& animPath,
                                                      const std::vector<animator::AnimationEvent>& events)
    {
        if (!std::filesystem::exists(animPath))
        {
            vfLogError("AnimationEventWriter: File does not exist: {}", animPath);
            return false;
        }

        // Use the canonical reader to find event data offset
        std::streampos eventOffset = resource::AnimationResource::getEventDataOffset(animPath);
        if (eventOffset == std::streampos(0))
        {
            vfLogError("AnimationEventWriter: Failed to get event data offset");
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
