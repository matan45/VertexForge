#pragma once

#include <glm/glm.hpp>

namespace water
{
    // VK-1607 review: the WaterWakeEmitterComponent emit decision, lifted out of OceanService so it
    // can be exercised without a registry, a scene or a ripple sim.
    //
    // The bug this exists to prevent: the emitter's stored position used to be rewritten ONLY when
    // an impulse was actually queued, so `travelled` accumulated across every frame the gates
    // rejected while `deltaTime` stayed one frame long. An emitter drifting at 0.05 m/s with a
    // 0.5 m/s minimum eventually accumulated 0.5 m of travel and reported 0.5 / (1/60) = 30 m/s,
    // sailed through the gate and stamped a full-strength ring - the precise thing Min Speed was
    // authored to suppress. Speed and spacing are two different measurements over two different
    // baselines, so they need two different anchors.

    // Per-emitter trail state. lastPositionXZ moves EVERY tick (it is the speed baseline);
    // lastEmitXZ moves only when an impulse is queued (it is the ring-spacing baseline).
    struct WakeTrailState
    {
        glm::vec2 lastPositionXZ{0.0f};
        glm::vec2 lastEmitXZ{0.0f};
    };

    // Speed over one tick. Returns 0 for a non-positive dt rather than dividing by it.
    [[nodiscard]] inline float wakeFrameSpeed(const glm::vec2& currentXZ, const glm::vec2& previousXZ,
                                              float deltaTime)
    {
        if (deltaTime <= 0.0f)
            return 0.0f;
        return glm::length(currentXZ - previousXZ) / deltaTime;
    }

    // Should this emitter stamp a ring now?
    //   speed              - metres/second over the LAST TICK ONLY (see wakeFrameSpeed)
    //   travelledSinceEmit - metres since the last ring, which is a different baseline
    [[nodiscard]] inline bool wakeShouldEmit(float speed, float travelledSinceEmit,
                                             float minSpeed, float travelInterval, bool continuous)
    {
        if (speed < minSpeed)
            return false;
        if (continuous)
            return true;
        return travelledSinceEmit >= travelInterval;
    }
}
