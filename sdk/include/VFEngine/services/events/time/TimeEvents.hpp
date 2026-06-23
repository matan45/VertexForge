#pragma once
#include "../EventTypes.hpp"
#include "time/FrameTime.hpp" // engineTime::FrameTime (utilities is on the Services include path)

namespace events::time
{
    // Set the global gameplay time scale (VK-992). The handler clamps the value to
    // the supported range before writing the Timer authority.
    struct SetGlobalTimeScaleCommand : ::events::ICommand<>
    {
        float scale = 1.0f;
        std::string_view getName() const override { return "SetGlobalTimeScale"; }
    };

    // Hard-freeze gameplay time (physics stops stepping, gameplay tasks get 0 delta)
    // while input/UI/render/loading keep running at real time.
    struct FreezeTimeCommand : ::events::ICommand<>
    {
        std::string_view getName() const override { return "FreezeTime"; }
    };

    // Release a previous FreezeTimeCommand; gameplay time resumes at the current scale.
    struct UnfreezeTimeCommand : ::events::ICommand<>
    {
        std::string_view getName() const override { return "UnfreezeTime"; }
    };

    // Coherent snapshot of the unscaled + scaled clocks for this frame.
    struct GetTimeSnapshotQuery : ::events::IQuery<engineTime::FrameTime>
    {
        std::string_view getName() const override { return "GetTimeSnapshot"; }
    };

    // Broadcast after the global time scale changes (carries the clamped value).
    struct GlobalTimeScaleChangedNotification : ::events::INotification
    {
        float scale = 1.0f;
        std::string_view getName() const override { return "GlobalTimeScaleChanged"; }
    };
}
