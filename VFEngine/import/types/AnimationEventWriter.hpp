#pragma once

#include "animator/AnimationEventTypes.hpp"
#include <string>
#include <vector>

namespace types
{
    class AnimationEventWriter
    {
    public:
        // Saves animation events into an existing .vfAnim file.
        // Events are appended after the channel data.
        // Preserves all existing animation data (header, channels).
        static bool saveEventsToAnimation(const std::string& animPath,
                                           const std::vector<animator::AnimationEvent>& events);
    };
}
