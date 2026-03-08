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

    struct AnimationLODConfigResult
    {
        float lod0Distance = 25.0f;
        float lod1Distance = 75.0f;
        float lod2Distance = 150.0f;
        float lod3Distance = 300.0f;
        uint32_t lod0Interval = 1;
        uint32_t lod1Interval = 2;
        uint32_t lod2Interval = 6;
        uint32_t maxStreamingInitPerFrame = 4;
    };

    struct GetAnimationLODConfigQuery : ::events::IQuery<AnimationLODConfigResult>
    {
        std::string_view getName() const override { return "GetAnimationLODConfig"; }
    };

    struct SetAnimationLODConfigCommand : ::events::ICommand<void>
    {
        float lod0Distance = 25.0f;
        float lod1Distance = 75.0f;
        float lod2Distance = 150.0f;
        float lod3Distance = 300.0f;
        uint32_t lod0Interval = 1;
        uint32_t lod1Interval = 2;
        uint32_t lod2Interval = 6;
        uint32_t maxStreamingInitPerFrame = 4;
        std::string_view getName() const override { return "SetAnimationLODConfig"; }
    };
}
