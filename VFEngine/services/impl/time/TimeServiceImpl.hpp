#pragma once
#include "../../interfaces/time/ITimeService.hpp"

namespace services
{
    class TimeServiceImpl : public ITimeService
    {
    public:
        TimeServiceImpl() = default;

        void registerEventHandlers() override;

        void setGlobalTimeScale(float scale) override;
        engineTime::FrameTime getTimeSnapshot() override;
        void freeze() override;
        void unfreeze() override;

        // Clamp a requested scale to the supported gameplay range [0, 8]. Exposed as a
        // free static helper so the CPU test suite can exercise the clamp directly.
        static float clampTimeScale(float scale);
    };
}
