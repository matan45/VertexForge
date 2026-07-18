#pragma once
#include <cstdint>

namespace services
{
    // Progress snapshot for async material-pipeline warm-up (VK-1532). Produced on the
    // Graphics side and polled by the loading screen via
    // events::render::GetPipelineWarmupStatsQuery. Lives in services/data so Graphics can
    // include it without depending on the Services event/EventDispatcher machinery.
    struct PipelineWarmupStats
    {
        uint32_t total = 0;     // materials scheduled for warm-up for the current scene
        uint32_t completed = 0; // warm-up jobs finished so far
        bool active = false;    // true while warm-up jobs are still running
    };
}
