#pragma once
#include <algorithm>

namespace navigation
{
    // Tuning for root-motion-driven NavmeshAgent pacing (VK-1408).
    struct RootMotionSpeedParams
    {
        float alpha = 0.2f;          // EMA smoothing factor in [0,1] (higher = snappier)
        float floorFraction = 0.15f; // minimum crowd speed as a fraction of maxSpeed
    };

    // Converts a root-motion clip's per-frame planar travel distance into the detour
    // crowd maxSpeed to use this frame.
    //
    // Why smoothing + a floor: a looping clip emits a zero root-motion delta on the
    // first frame and on every loop wrap (AnimatorStateMachine::updateRootMotionDelta).
    // A naive distance/dt would read that single zero frame as a full stop and could
    // trip the agent's stuck-timer. The EMA absorbs the one-frame zero as a small dip,
    // and the floor guarantees a moving agent never drops to a stall speed mid-stride.
    //
    // smoothedInOut holds the (unfloored) EMA state and is updated in place; the floor
    // is applied only to the returned crowd speed so it does not ratchet the state.
    // Near the destination the crowd zeroes its own desired velocity regardless of
    // maxSpeed, so flooring maxSpeed does not prevent a clean stop.
    inline float computeRootMotionCrowdSpeed(float planarDistance, float dt,
                                             float& smoothedInOut, float maxSpeedCap,
                                             RootMotionSpeedParams p = {})
    {
        const float inst = (dt > 1e-5f) ? planarDistance / dt : smoothedInOut;
        const float clampedInst = std::clamp(inst, 0.0f, maxSpeedCap);
        smoothedInOut += p.alpha * (clampedInst - smoothedInOut);
        const float floorSpeed = p.floorFraction * maxSpeedCap;
        return std::max(smoothedInOut, floorSpeed);
    }
}
