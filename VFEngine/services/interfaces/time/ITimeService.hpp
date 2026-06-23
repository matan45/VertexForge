#pragma once
#include "time/FrameTime.hpp" // engineTime::FrameTime (utilities is on the Services include path)

namespace services
{
    // Runtime gameplay-time authority facade (VK-992). Forwards scale/freeze writes
    // to the engineTime::Timer statics and exposes a coherent FrameTime snapshot.
    // Pure CPU state forwarding — no provider/adapter is required.
    class ITimeService
    {
    public:
        virtual ~ITimeService() = default;

        virtual void registerEventHandlers() = 0;

        virtual void setGlobalTimeScale(float scale) = 0;
        virtual engineTime::FrameTime getTimeSnapshot() = 0;
        virtual void freeze() = 0;
        virtual void unfreeze() = 0;
    };
}
