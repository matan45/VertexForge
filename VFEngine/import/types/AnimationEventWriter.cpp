#include "AnimationEventWriter.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/AnimationResource.hpp"

#include <vector>
#include <filesystem>

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

        std::streampos eventOffset = resource::AnimationResource::getEventDataOffset(animPath);
        if (eventOffset == std::streampos(0))
        {
            vfLogError("AnimationEventWriter: Failed to get event data offset");
            return false;
        }

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

        {
            std::ofstream file(animPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                vfLogError("AnimationEventWriter: Cannot open file for writing: {}", animPath);
                return false;
            }

            file.write(prefixData.data(), static_cast<std::streamsize>(prefixData.size()));

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

    void AnimationEventWriter::writeString(std::ofstream& file, const std::string& str)
    {
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(str.size()));
        if (!str.empty())
        {
            file.write(str.data(), str.size());
        }
    }
}
