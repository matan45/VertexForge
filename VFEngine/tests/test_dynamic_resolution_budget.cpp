#include <doctest.h>
#include <render/upscaling/DynamicResolutionBudget.hpp>

#include <vector>

// ============================================================
// evaluateDynamicResolutionCore — the pure adaptive dynamic-resolution decision (VK-1531),
// mirroring test_rt_shadow_budget. No Vulkan, so the CPU-only Tests project pins down:
//   * the guards (disabled / no frame-time sample hold the scale and clear the streak),
//   * the EMA-vs-target deadband counter maintenance (over-budget / under-budget / in-band),
//   * the hysteresis thresholds (nothing happens until the streak reaches the limit),
//   * the step direction + size (down by downStep when over budget, up by upStep when under),
//   * the clamp floors/ceiling (never steps below minScale or above maxScale),
//   * end-to-end convergence: a closed-loop cost model settles at a stable scale under budget.
// The whole decision — the EMA comparison AND the streak counters — now lives in this one
// function (the comparison previously lived in the OffScreenViewPort tick), so the caller just
// carries appliedScale + the counters across frames.
// ============================================================

using render::upscaling::DynResInputs;
using render::upscaling::DynResDecision;
using render::upscaling::evaluateDynamicResolutionCore;

namespace
{
    // A baseline that is OVER budget (ema 33 vs target 16.6), adaptive on, sample present, room to
    // step both ways. Individual cases override emaFrameGpuMs to drive the under-budget / in-band
    // paths (under-budget band edge = 16.6 * 0.7 = 11.62 ms).
    DynResInputs makeInputs()
    {
        DynResInputs in{};
        in.enabled = true;
        in.haveFrameTime = true;
        in.emaFrameGpuMs = 33.0f; // over budget
        in.targetMs = 16.6f;
        in.restoreThreshold = 0.7f;
        in.framesOverBudget = 0;
        in.framesUnderBudget = 0;
        in.hysteresisFramesDown = 10;
        in.hysteresisFramesUp = 30;
        in.appliedScale = 1.0f;
        in.minScale = 0.5f;
        in.maxScale = 1.0f;
        in.downStep = 0.10f;
        in.upStep = 0.05f;
        return in;
    }

    enum class Step { None, Down, Up };

    Step classify(const DynResDecision& d, float prevScale)
    {
        if (!d.newScale.has_value()) return Step::None;
        return d.newScale.value() < prevScale ? Step::Down : Step::Up;
    }
}

TEST_CASE("evaluateDynamicResolutionCore: guards — no adaptation when disabled or no frame time")
{
    SUBCASE("disabled holds the scale and clears the streak")
    {
        DynResInputs in = makeInputs();
        in.enabled = false;
        in.framesOverBudget = 999; // would step down if it were enabled
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None);
        CHECK(d.appliedScale == doctest::Approx(1.0f));
        CHECK(d.framesOverBudget == 0u); // no usable signal -> streak cleared
    }

    SUBCASE("no completed frame-time sample holds the scale and clears the streak")
    {
        DynResInputs in = makeInputs();
        in.haveFrameTime = false;
        in.framesOverBudget = 999;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None);
        CHECK(d.framesOverBudget == 0u);
    }
}

TEST_CASE("evaluateDynamicResolutionCore: EMA-vs-target maintains the deadband counters")
{
    SUBCASE("over budget increments the over-streak and clears the under-streak")
    {
        DynResInputs in = makeInputs();
        in.emaFrameGpuMs = 33.0f; // > target
        in.framesOverBudget = 3;
        in.framesUnderBudget = 2;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(d.framesOverBudget == 4u);
        CHECK(d.framesUnderBudget == 0u);
        CHECK(classify(d, in.appliedScale) == Step::None); // still below the down threshold
    }

    SUBCASE("under budget increments the under-streak and clears the over-streak")
    {
        DynResInputs in = makeInputs();
        in.emaFrameGpuMs = 10.0f; // < target * restoreThreshold (11.62)
        in.appliedScale = 0.8f;
        in.framesOverBudget = 2;
        in.framesUnderBudget = 3;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(d.framesUnderBudget == 4u);
        CHECK(d.framesOverBudget == 0u);
        CHECK(classify(d, in.appliedScale) == Step::None);
    }

    SUBCASE("in the deadband both streaks reset")
    {
        DynResInputs in = makeInputs();
        in.emaFrameGpuMs = 14.0f; // between 11.62 and 16.6 -> no-action band
        in.framesOverBudget = 5;
        in.framesUnderBudget = 5;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(d.framesOverBudget == 0u);
        CHECK(d.framesUnderBudget == 0u);
        CHECK(classify(d, in.appliedScale) == Step::None);
    }
}

TEST_CASE("evaluateDynamicResolutionCore: hysteresis thresholds gate the action")
{
    SUBCASE("one frame short of the down threshold does nothing")
    {
        DynResInputs in = makeInputs();
        in.framesOverBudget = 8; // + this over-budget frame -> 9, still < 10
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None);
        CHECK(d.framesOverBudget == 9u); // streak advanced but not consumed
    }

    SUBCASE("reaching the down threshold steps down and resets the streak")
    {
        DynResInputs in = makeInputs();
        in.framesOverBudget = 9; // + this over-budget frame -> 10 == threshold
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::Down);
        CHECK(d.newScale.value() == doctest::Approx(0.9f)); // 1.0 - downStep
        CHECK(d.appliedScale == doctest::Approx(0.9f));
        CHECK(d.framesOverBudget == 0u);
    }

    SUBCASE("one frame short of the up threshold does nothing")
    {
        DynResInputs in = makeInputs();
        in.emaFrameGpuMs = 10.0f; // under budget
        in.appliedScale = 0.8f;   // room to recover
        in.framesUnderBudget = 28; // + this frame -> 29, still < 30
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None);
        CHECK(d.framesUnderBudget == 29u);
    }

    SUBCASE("reaching the up threshold steps up and resets the streak")
    {
        DynResInputs in = makeInputs();
        in.emaFrameGpuMs = 10.0f;
        in.appliedScale = 0.8f;
        in.framesUnderBudget = 29; // + this frame -> 30 == threshold
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::Up);
        CHECK(d.newScale.value() == doctest::Approx(0.85f)); // 0.8 + upStep
        CHECK(d.framesUnderBudget == 0u);
    }
}

TEST_CASE("evaluateDynamicResolutionCore: clamps to the [minScale, maxScale] band")
{
    SUBCASE("over budget at the floor does not step below minScale")
    {
        DynResInputs in = makeInputs();
        in.appliedScale = 0.5f;   // == minScale
        in.framesOverBudget = 9;  // -> 10 this over-budget frame
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None); // no room to go lower
        CHECK(d.appliedScale == doctest::Approx(0.5f));
        CHECK(d.framesOverBudget == 0u); // streak still reset after the (no-op) action
    }

    SUBCASE("under budget at the ceiling does not step above maxScale")
    {
        DynResInputs in = makeInputs();
        in.emaFrameGpuMs = 10.0f;
        in.appliedScale = 1.0f;    // == maxScale
        in.framesUnderBudget = 29; // -> 30 this under-budget frame
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None);
        CHECK(d.appliedScale == doctest::Approx(1.0f));
        CHECK(d.framesUnderBudget == 0u);
    }

    SUBCASE("a down step never overshoots the floor")
    {
        DynResInputs in = makeInputs();
        in.appliedScale = 0.55f; // one downStep would land below minScale
        in.framesOverBudget = 9;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::Down);
        CHECK(d.newScale.value() == doctest::Approx(0.5f)); // clamped to minScale, not 0.45
    }

    SUBCASE("an up step never overshoots the ceiling")
    {
        DynResInputs in = makeInputs();
        in.emaFrameGpuMs = 10.0f;
        in.appliedScale = 0.98f; // one upStep would land above maxScale
        in.framesUnderBudget = 29;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::Up);
        CHECK(d.newScale.value() == doctest::Approx(1.0f)); // clamped to maxScale, not 1.03
    }
}

TEST_CASE("evaluateDynamicResolutionCore: throttled reflects a below-native scale")
{
    SUBCASE("native scale is not throttled")
    {
        DynResInputs in = makeInputs();
        in.appliedScale = 1.0f;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK_FALSE(d.throttled);
    }

    SUBCASE("a reduced scale is throttled even through the guard")
    {
        DynResInputs in = makeInputs();
        in.enabled = false; // guard path still computes throttled
        in.appliedScale = 0.8f;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(d.throttled);
    }
}

TEST_CASE("evaluateDynamicResolutionCore: closed-loop convergence settles under budget without oscillation")
{
    // Cost model: whole-frame GPU ms is proportional to the render scale (fewer pixels -> less
    // time). baseline at scale 1.0 is well over budget, so the governor must step down and settle
    // in the deadband. The core now does the counter maintenance, so the loop just feeds it ema.
    const float target = 16.6f;
    const float baseline = 25.0f; // 25 * scale == target at scale 0.664 -> settles ~0.6..0.7

    float scale = 1.0f;
    uint32_t over = 0;
    uint32_t under = 0;
    float scaleAt200 = -1.0f;
    float lastEma = 0.0f;

    for (int i = 0; i < 500; ++i)
    {
        const float ema = baseline * scale;
        lastEma = ema;

        DynResInputs in = makeInputs();
        in.emaFrameGpuMs = ema;
        in.appliedScale = scale;
        in.framesOverBudget = over;
        in.framesUnderBudget = under;

        DynResDecision d = evaluateDynamicResolutionCore(in);
        over = d.framesOverBudget;
        under = d.framesUnderBudget;
        scale = d.appliedScale;

        // Never leaves the allowed band at any point.
        CHECK(scale >= 0.5f - 1e-5f);
        CHECK(scale <= 1.0f + 1e-5f);

        if (i == 200) scaleAt200 = scale;
    }

    // Converged: the scale is stable across the back half (no oscillation)...
    CHECK(scaleAt200 == doctest::Approx(scale));
    // ...it actually engaged (dropped below native since baseline > target)...
    CHECK(scale < 1.0f);
    // ...and the settled frame time is at or under budget.
    CHECK(lastEma <= target + 1e-4f);
}
