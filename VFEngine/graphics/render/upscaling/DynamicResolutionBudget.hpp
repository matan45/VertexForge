#pragma once

#include <cstdint>
#include <optional>
#include <algorithm>

namespace render::upscaling
{
    // VK-1531: pure adaptive dynamic-resolution math, mirroring the RTShadowBudget.hpp pattern
    // (render::raytracing::evaluateBudgetCore) so the CPU-only Tests project can validate the
    // hysteresis thresholds and the step/clamp behavior with no Vulkan device
    // (test_dynamic_resolution_budget). Header-only, dependency-free, and the single source of
    // truth the OffScreenViewPort tick calls into. Keeping the decision in one testable function
    // guarantees the behavior the tick applies is exactly the behavior the tests pin down.
    //
    // The controller reacts to the EMA-smoothed whole-frame GPU cost (GpuPassStats::emaFrameGpuMs)
    // and drives a single continuous scale in [minScale, maxScale] that ResolutionManager folds
    // into the render resolution (renderRes = display * scale / presetScaleFactor). It layers UNDER
    // a pinned upscaler quality mode: scale 1.0 is the mode's nominal render size, scale < 1.0
    // pushes the pre-upscale render resolution lower within the allowed band.
    //
    // Deadband hysteresis (matches RTShadowProfiler): a streak of frames strictly over the target
    // steps the scale DOWN; a streak of frames under target * restoreThreshold steps it UP. The
    // band between (restoreThreshold..1.0 of target) takes no action, so the scale never flaps
    // around the target. The step-DOWN threshold is smaller than step-UP (react fast when over
    // budget, creep back slowly). The EMA-vs-target comparison AND the streak counters are both
    // maintained inside evaluateDynamicResolutionCore; the caller only carries appliedScale and the
    // counters across frames, so this one function is the whole decision.

    // Smoothed controller state fed into the decision. Plain data, no Vulkan.
    struct DynResInputs
    {
        bool enabled = false;
        // Whether GpuPassStats has a completed whole-frame GPU sample yet (analogous to
        // RTShadowBudget's emaInitialized guard). No sample -> no adaptation.
        bool haveFrameTime = false;

        // EMA-smoothed whole-frame GPU cost vs. the target (both milliseconds).
        float emaFrameGpuMs = 0.0f;
        float targetMs = 16.6f;
        // Step up only once cost drops below target * restoreThreshold (deadband hysteresis).
        float restoreThreshold = 0.7f;

        // Deadband streak counters carried across frames by the caller; incremented / reset here
        // from the EMA-vs-target comparison. Plus the frame-count action thresholds.
        uint32_t framesOverBudget = 0;
        uint32_t framesUnderBudget = 0;
        uint32_t hysteresisFramesDown = 10;
        uint32_t hysteresisFramesUp = 30;

        // Current continuous render scale and its allowed band + per-step size.
        float appliedScale = 1.0f;
        float minScale = 0.5f;
        float maxScale = 1.0f;
        float downStep = 0.10f; // react fast when over budget
        float upStep = 0.05f;   // creep back to damp oscillation
    };

    // Result of the decision: the per-frame action (nullopt == unchanged) plus the updated state
    // the caller writes back. Splitting the action from the applied state keeps the caller's
    // write-back trivial and lets the tests assert on both the command and the resulting state.
    struct DynResDecision
    {
        std::optional<float> newScale;

        float appliedScale = 1.0f;
        uint32_t framesOverBudget = 0;
        uint32_t framesUnderBudget = 0;
        bool throttled = false; // appliedScale < maxScale (rendering below native)
    };

    inline DynResDecision evaluateDynamicResolutionCore(const DynResInputs& in)
    {
        DynResDecision d{};
        // Carry current state through unchanged by default; the steps below override selectively.
        d.appliedScale = in.appliedScale;
        d.framesOverBudget = in.framesOverBudget;
        d.framesUnderBudget = in.framesUnderBudget;

        auto computeThrottled = [&]() { return d.appliedScale < in.maxScale; };

        if (!in.enabled || !in.haveFrameTime)
        {
            // No usable GPU-time signal this frame: hold the scale and clear the streak so a gap in
            // timing (or the feature being off) never carries a stale partial run across. This
            // matches the pre-refactor tick, which reset both counters whenever there was no sample.
            d.framesOverBudget = 0;
            d.framesUnderBudget = 0;
            d.throttled = computeThrottled();
            return d;
        }

        // Deadband hysteresis counter maintenance: a frame whose EMA-smoothed cost is strictly over
        // target counts toward a step DOWN; a frame under target * restoreThreshold counts toward a
        // step UP; the band between resets both, so the scale never flaps around the target. This
        // comparison used to live in OffScreenViewPort::tickDynamicResolution — it is here now so
        // this unit-tested function is the true single source of truth for the whole decision.
        if (in.emaFrameGpuMs > in.targetMs)
        {
            d.framesOverBudget = in.framesOverBudget + 1;
            d.framesUnderBudget = 0;
        }
        else if (in.emaFrameGpuMs < in.targetMs * in.restoreThreshold)
        {
            d.framesUnderBudget = in.framesUnderBudget + 1;
            d.framesOverBudget = 0;
        }
        else
        {
            d.framesOverBudget = 0;
            d.framesUnderBudget = 0;
        }

        // Throttle down: sustained over budget -> lower the render scale one step.
        if (d.framesOverBudget >= in.hysteresisFramesDown)
        {
            if (in.appliedScale > in.minScale)
            {
                d.newScale = std::max(in.minScale, in.appliedScale - in.downStep);
            }
            d.framesOverBudget = 0; // reset after taking action (even at the floor)
        }
        // Restore: sustained under budget -> raise the render scale one step.
        else if (d.framesUnderBudget >= in.hysteresisFramesUp)
        {
            if (in.appliedScale < in.maxScale)
            {
                d.newScale = std::min(in.maxScale, in.appliedScale + in.upStep);
            }
            d.framesUnderBudget = 0; // reset after taking action (even at the ceiling)
        }

        if (d.newScale.has_value())
        {
            d.appliedScale = d.newScale.value();
        }

        d.throttled = computeThrottled();
        return d;
    }
}
