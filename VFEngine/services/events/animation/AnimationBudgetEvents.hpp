#pragma once

#include "../EventTypes.hpp"
#include <cstdint>

namespace services::events::animation
{
    struct AnimationBudgetStatsResult
    {
        uint32_t totalAnimators = 0;
        uint32_t culledEntities = 0;
        uint32_t lodCounts[4] = {0, 0, 0, 0};
        uint32_t allocatedBoneMatrices = 0;
        uint32_t maxBoneMatrices = 0;
        float boneFragmentationPercent = 0.0f;
        uint32_t pendingStreamingInits = 0;
    };

    struct GetAnimationBudgetStatsQuery : ::events::IQuery<AnimationBudgetStatsResult>
    {
        std::string_view getName() const override { return "GetAnimationBudgetStats"; }
    };
}
