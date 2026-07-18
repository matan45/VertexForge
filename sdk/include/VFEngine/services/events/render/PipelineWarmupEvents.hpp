#pragma once
#include "../EventTypes.hpp"
#include "../../data/PipelineWarmupTypes.hpp"

namespace events::render
{
    // Read the current async pipeline warm-up progress (VK-1532). Handled by the Core
    // PipelineWarmupAdapter; the runtime/editor loading screen polls it each frame to
    // drive the "init" progress band and keep the loading screen up until warm-up ends.
    struct GetPipelineWarmupStatsQuery : ::events::IQuery<services::PipelineWarmupStats>
    {
        std::string_view getName() const override { return "GetPipelineWarmupStats"; }
    };
}
