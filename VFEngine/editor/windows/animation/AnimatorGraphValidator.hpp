#pragma once

#include "animator/AnimatorTypes.hpp"
#include <string>
#include <vector>

namespace windows::animation
{
    struct GraphWarning
    {
        enum class Level { Warning, Error };
        Level level = Level::Warning;
        std::string message;
        uint32_t stateId = 0;       // 0 if not state-specific
        uint32_t transitionId = 0;  // 0 if not transition-specific
    };

    class AnimatorGraphValidator
    {
    public:
        static std::vector<GraphWarning> validate(const animator::AnimatorData& data);

    private:
        static void checkDeadEndStates(const animator::AnimatorGraph& graph, std::vector<GraphWarning>& warnings);
        static void checkUnreachableStates(const animator::AnimatorGraph& graph, std::vector<GraphWarning>& warnings);
        static void checkMissingAnimations(const animator::AnimatorGraph& graph, std::vector<GraphWarning>& warnings);
        static void checkUndefinedParameters(const animator::AnimatorGraph& graph, std::vector<GraphWarning>& warnings);
        static void checkNoDefaultState(const animator::AnimatorGraph& graph, std::vector<GraphWarning>& warnings);
    };
}
