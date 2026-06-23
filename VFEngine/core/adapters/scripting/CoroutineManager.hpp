#pragma once

#include <value/AsyncPromiseValue.hpp>
#include <value/ValueType.hpp>
#include <vector>
#include <cstdint>
#include <memory>

namespace core
{
    class CoroutineManager
    {
    public:
        enum class WaitType { Seconds, RealSeconds, Frames, FixedUpdate };

        struct PendingWait
        {
            uint64_t id;
            uint64_t scriptInstanceId;
            std::shared_ptr<value::AsyncPromiseValue> promise;
            WaitType type;
            double resolveTime = 0.0;
            int framesRemaining = 0;
        };

        CoroutineManager() = default;
        ~CoroutineManager() = default;

        value::Value waitForSeconds(uint64_t instanceId, double seconds);
        value::Value waitForRealSeconds(uint64_t instanceId, double seconds);
        value::Value waitForFrames(uint64_t instanceId, int frames);
        value::Value waitForFixedUpdate(uint64_t instanceId);

        void tickFrame(double scaledDelta, double unscaledDelta);
        void tickFixedUpdate();

        void removeAllForInstance(uint64_t instanceId);
        void clear();

    private:
        std::vector<PendingWait> pendingWaits;
        double scaledElapsed = 0.0;
        double unscaledElapsed = 0.0;
        uint64_t nextWaitId = 1;
    };
}
