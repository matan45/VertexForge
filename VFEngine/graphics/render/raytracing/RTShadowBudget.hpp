#pragma once

#include <cstdint>
#include <optional>
#include <algorithm>

namespace render::raytracing
{
    // VK-1479 C5: pure adaptive RT-shadow budget math, factored out of RTShadowProfiler so the
    // CPU-only Tests project can validate the hysteresis thresholds and the degrade/upgrade
    // ordering with no Vulkan device (test_rt_shadow_budget). Mirrors the ShadowPageOverlap.hpp
    // pattern: header-only, dependency-free, and the single source of truth that the profiler
    // calls into. Keeping the decision in one testable function guarantees the ordering the
    // profiler applies is exactly the ordering the tests pin down.
    //
    // Degrade order (sustained over budget), first applicable step wins:
    //   directional spatial passes -> directional ray distance -> spot budget -> point budget -> skip
    // Upgrade order (sustained under budget) is the exact reverse:
    //   unskip -> point budget -> spot budget -> ray distance -> spatial passes
    // When spot/point RT are inactive the spot/point steps are skipped, so the order collapses to
    // the pre-C5 directional-only order (spatial passes -> ray distance -> skip) and behavior is
    // byte-identical.

    // Smoothed profiler state fed into the decision. Plain data, no Vulkan.
    struct BudgetInputs
    {
        bool adaptiveEnabled = true;
        bool emaInitialized = false;

        // EMA-smoothed total RT-shadow GPU cost (directional + spot + point) vs. the frame budget.
        float emaTotalMs = 0.0f;
        float budgetMs = 2.0f;
        // Restore only once cost drops below budget * restoreThreshold (deadband hysteresis).
        float restoreThreshold = 0.7f;

        // Hysteresis counters (maintained by the profiler in readbackAndUpdate) and thresholds.
        uint32_t framesOverBudget = 0;
        uint32_t framesUnderBudget = 0;
        uint32_t hysteresisFramesDown = 10;
        uint32_t hysteresisFramesUp = 30;

        // Directional levers.
        float baseMaxRayDistance = 500.0f;
        float appliedMaxRayDistance = 500.0f;
        int baseSpatialPasses = 3;
        int appliedSpatialPasses = 3;

        // Spot / point RT light-count budgets. *Active == false collapses the corresponding step
        // out of the ordering (see header note), so a directional-only setup stays byte-identical.
        bool spotActive = false;
        uint32_t baseSpotBudget = 0;
        uint32_t appliedSpotBudget = 0;
        bool pointActive = false;
        uint32_t basePointBudget = 0;
        uint32_t appliedPointBudget = 0;

        // Whether a frame-skip is currently in effect (persistent skip state).
        bool skipActive = false;
    };

    // Result of the decision: the per-frame actions (nullopt == unchanged) plus the updated state
    // the profiler writes back. Splitting "actions" from "applied state" keeps the profiler's
    // write-back trivial and lets the tests assert on both the command and the resulting state.
    struct BudgetDecision
    {
        std::optional<float> newMaxRayDistance;
        std::optional<int> newSpatialPasses;
        std::optional<uint32_t> newSpotBudget;
        std::optional<uint32_t> newPointBudget;
        bool skipFrame = false;

        float appliedMaxRayDistance = 500.0f;
        int appliedSpatialPasses = 3;
        uint32_t appliedSpotBudget = 0;
        uint32_t appliedPointBudget = 0;
        bool skipActive = false;
        bool throttled = false;
        uint32_t framesOverBudget = 0;
        uint32_t framesUnderBudget = 0;
    };

    inline BudgetDecision evaluateBudgetCore(const BudgetInputs& in)
    {
        BudgetDecision d{};
        // Carry current applied state through unchanged by default; steps below override selectively.
        d.appliedMaxRayDistance = in.appliedMaxRayDistance;
        d.appliedSpatialPasses = in.appliedSpatialPasses;
        d.appliedSpotBudget = in.appliedSpotBudget;
        d.appliedPointBudget = in.appliedPointBudget;
        d.framesOverBudget = in.framesOverBudget;
        d.framesUnderBudget = in.framesUnderBudget;
        d.skipActive = in.skipActive;

        auto computeThrottled = [&]()
        {
            return (d.appliedMaxRayDistance < in.baseMaxRayDistance) ||
                   (d.appliedSpatialPasses < in.baseSpatialPasses) ||
                   d.skipActive ||
                   (in.spotActive && d.appliedSpotBudget < in.baseSpotBudget) ||
                   (in.pointActive && d.appliedPointBudget < in.basePointBudget);
        };

        if (!in.adaptiveEnabled || !in.emaInitialized)
        {
            d.throttled = computeThrottled();
            return d;
        }

        // Throttle down: sustained over budget -> reduce quality one step.
        if (in.framesOverBudget >= in.hysteresisFramesDown)
        {
            if (in.appliedSpatialPasses > 1)
            {
                d.newSpatialPasses = in.appliedSpatialPasses - 1;
            }
            else if (in.appliedMaxRayDistance > in.baseMaxRayDistance * 0.25f)
            {
                d.newMaxRayDistance = in.appliedMaxRayDistance * 0.75f;
            }
            else if (in.spotActive && in.appliedSpotBudget > 1)
            {
                d.newSpotBudget = std::max(1u, in.appliedSpotBudget / 2u);
            }
            else if (in.pointActive && in.appliedPointBudget > 1)
            {
                d.newPointBudget = std::max(1u, in.appliedPointBudget / 2u);
            }
            else
            {
                d.skipFrame = true;
            }
            d.framesOverBudget = 0; // reset after taking action
        }
        // Restore quality: sustained under budget -> raise quality one step (reverse order).
        else if (in.framesUnderBudget >= in.hysteresisFramesUp)
        {
            if (in.skipActive)
            {
                // Stop skipping first; skipFrame stays false, skipActive cleared below.
            }
            else if (in.pointActive && in.appliedPointBudget < in.basePointBudget)
            {
                d.newPointBudget = std::min(in.basePointBudget,
                                            std::max(in.appliedPointBudget * 2u, in.appliedPointBudget + 1u));
            }
            else if (in.spotActive && in.appliedSpotBudget < in.baseSpotBudget)
            {
                d.newSpotBudget = std::min(in.baseSpotBudget,
                                           std::max(in.appliedSpotBudget * 2u, in.appliedSpotBudget + 1u));
            }
            else if (in.appliedMaxRayDistance < in.baseMaxRayDistance)
            {
                float step = (in.baseMaxRayDistance - in.appliedMaxRayDistance) * 0.1f;
                float nv = in.appliedMaxRayDistance + std::max(step, 10.0f);
                if (nv > in.baseMaxRayDistance) nv = in.baseMaxRayDistance;
                d.newMaxRayDistance = nv;
            }
            else if (in.appliedSpatialPasses < in.baseSpatialPasses)
            {
                d.newSpatialPasses = in.appliedSpatialPasses + 1;
            }
            d.framesUnderBudget = 0; // reset after taking action
        }

        if (d.newMaxRayDistance.has_value()) d.appliedMaxRayDistance = d.newMaxRayDistance.value();
        if (d.newSpatialPasses.has_value()) d.appliedSpatialPasses = d.newSpatialPasses.value();
        if (d.newSpotBudget.has_value()) d.appliedSpotBudget = d.newSpotBudget.value();
        if (d.newPointBudget.has_value()) d.appliedPointBudget = d.newPointBudget.value();

        // Legacy skip-state semantics: skipActive becomes true only on the exact frame a skip is
        // commanded, and otherwise resolves to false (whether or not we were skipping before).
        d.skipActive = d.skipFrame;

        d.throttled = computeThrottled();
        return d;
    }
}
