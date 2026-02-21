#pragma once

#include "animator/AnimationEventTypes.hpp"
#include <fstream>
#include <string>
#include <vector>

namespace types
{
    class AnimationEventWriter
    {
    public:
        static bool saveEventsToAnimation(const std::string& animPath,
                                           const std::vector<animator::AnimationEvent>& events);

    private:
        static void writeString(std::ofstream& file, const std::string& str);
    };
}
