#include "AnimationEventIO.hpp"
#include "print/EditorLogger.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

namespace types
{
    std::string AnimationEventIO::getEventsFilePath(const std::string& animPath)
    {
        return animPath + ".events.json";
    }

    bool AnimationEventIO::saveEvents(const std::string& animPath,
                                       const std::vector<animator::AnimationEvent>& events)
    {
        std::string eventsPath = getEventsFilePath(animPath);

        json j = json::array();
        for (const auto& event : events)
        {
            json eventJson;
            eventJson["name"] = event.name;
            eventJson["normalizedTime"] = event.normalizedTime;
            eventJson["payload"] = event.payload;
            j.push_back(eventJson);
        }

        std::ofstream file(eventsPath);
        if (!file.is_open())
        {
            vfLogError("AnimationEventIO: Cannot open file for writing: {}", eventsPath);
            return false;
        }

        file << j.dump(4);

        if (file.fail())
        {
            vfLogError("AnimationEventIO: Failed to write events file: {}", eventsPath);
            return false;
        }

        vfLogInfo("AnimationEventIO: Saved {} events to {}", events.size(), eventsPath);
        return true;
    }

    std::vector<animator::AnimationEvent> AnimationEventIO::loadEvents(const std::string& animPath)
    {
        std::string eventsPath = getEventsFilePath(animPath);
        std::vector<animator::AnimationEvent> events;

        if (!std::filesystem::exists(eventsPath))
        {
            return events;
        }

        std::ifstream file(eventsPath);
        if (!file.is_open())
        {
            vfLogError("AnimationEventIO: Cannot open events file: {}", eventsPath);
            return events;
        }

        try
        {
            json j;
            file >> j;

            if (!j.is_array())
            {
                vfLogError("AnimationEventIO: Events file is not a JSON array");
                return events;
            }

            for (const auto& eventJson : j)
            {
                animator::AnimationEvent event;
                event.name = eventJson.value("name", "");
                event.normalizedTime = eventJson.value("normalizedTime", 0.0f);
                event.payload = eventJson.value("payload", "");
                events.push_back(event);
            }

            vfLogInfo("AnimationEventIO: Loaded {} events from {}", events.size(), eventsPath);
        }
        catch (const json::exception& e)
        {
            vfLogError("AnimationEventIO: Failed to parse events file: {}", e.what());
        }

        return events;
    }
}
