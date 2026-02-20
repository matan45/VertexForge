#pragma once

#include "animator/AnimationEventTypes.hpp"
#include <string>
#include <vector>

namespace types
{
    class AnimationEventIO
    {
    public:
        // Saves animation events to a JSON file alongside the .vfAnim file.
        // File is saved as "{animPath}.events.json"
        static bool saveEvents(const std::string& animPath,
                               const std::vector<animator::AnimationEvent>& events);

        // Loads animation events from the companion JSON file.
        // Returns empty vector if no file exists.
        static std::vector<animator::AnimationEvent> loadEvents(const std::string& animPath);

    private:
        static std::string getEventsFilePath(const std::string& animPath);
    };
}
