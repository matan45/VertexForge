#include <doctest.h>
#include <render/upscaling/DynamicResolutionBudget.hpp>

#include <vector>

// ============================================================
// evaluateDynamicResolutionCore — the pure adaptive dynamic-resolution decision (VK-1531),
// mirroring test_rt_shadow_budget. No Vulkan, so the CPU-only Tests project pins down:
//   * the guards (disabled / no frame-time sample leave the scale untouched, streak preserved),
//   * the hysteresis thresholds (nothing happens until framesOver/Under reach the limits),
//   * the step direction + size (down by downStep when over budget, up by upStep when under),
//   * the clamp floors/ceiling (never steps below minScale or above maxScale),
//   * end-to-end convergence: a closed-loop cost model settles at a stable scale under budget
//     with no oscillation.
// The EMA + over/under streak counters live in the caller (the OffScreenViewPort tick), so the
// convergence test replicates that 3-branch counter maintenance exactly.
// ============================================================

using render::upscaling::DynResInputs;
using render::upscaling::DynResDecision;
using render::upscaling::evaluateDynamicResolutionCore;

namespace
{
    // A mid-band baseline: adaptive on, a frame-time sample present, room to step both ways.
    DynResInputs makeInputs()
    {
        DynResInputs in{};
        in.enabled = true;
        in.haveFrameTime = true;
        in.emaFrameGpuMs = 33.0f; // over budget (unused by the decision except via the counters)
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
    SUBCASE("disabled leaves the scale untouched and does not consume the streak")
    {
        DynResInputs in = makeInputs();
        in.enabled = false;
        in.framesOverBudget = 999; // would step down if it were enabled
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None);
        CHECK(d.appliedScale == doctest::Approx(1.0f));
        CHECK(d.framesOverBudget == 999u);
    }

    SUBCASE("no completed frame-time sample leaves the scale untouched")
    {
        DynResInputs in = makeInputs();
        in.haveFrameTime = false;
        in.framesOverBudget = 999;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None);
        CHECK(d.framesOverBudget == 999u);
    }
}

TEST_CASE("evaluateDynamicResolutionCore: hysteresis thresholds gate the action")
{
    SUBCASE("one frame short of the down threshold does nothing")
    {
        DynResInputs in = makeInputs();
        in.framesOverBudget = 9; // hysteresisFramesDown - 1
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None);
        CHECK(d.framesOverBudget == 9u); // streak preserved
    }

    SUBCASE("reaching the down threshold steps down and resets the streak")
    {
        DynResInputs in = makeInputs();
        in.framesOverBudget = 10;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::Down);
        CHECK(d.newScale.value() == doctest::Approx(0.9f)); // 1.0 - downStep
        CHECK(d.appliedScale == doctest::Approx(0.9f));
        CHECK(d.framesOverBudget == 0u);
    }

    SUBCASE("one frame short of the up threshold does nothing")
    {
        DynResInputs in = makeInputs();
        in.appliedScale = 0.8f; // room to recover
        in.framesUnderBudget = 29; // hysteresisFramesUp - 1
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None);
        CHECK(d.framesUnderBudget == 29u);
    }

    SUBCASE("reaching the up threshold steps up and resets the streak")
    {
        DynResInputs in = makeInputs();
        in.appliedScale = 0.8f;
        in.framesUnderBudget = 30;
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
        in.appliedScale = 0.5f; // == minScale
        in.framesOverBudget = 10;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None); // no room to go lower
        CHECK(d.appliedScale == doctest::Approx(0.5f));
        CHECK(d.framesOverBudget == 0u); // streak still reset after the (no-op) action
    }

    SUBCASE("under budget at the ceiling does not step above maxScale")
    {
        DynResInputs in = makeInputs();
        in.appliedScale = 1.0f; // == maxScale
        in.framesUnderBudget = 30;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::None);
        CHECK(d.appliedScale == doctest::Approx(1.0f));
        CHECK(d.framesUnderBudget == 0u);
    }

    SUBCASE("a down step never overshoots the floor")
    {
        DynResInputs in = makeInputs();
        in.appliedScale = 0.55f; // one downStep would land below minScale
        in.framesOverBudget = 10;
        DynResDecision d = evaluateDynamicResolutionCore(in);
        CHECK(classify(d, in.appliedScale) == Step::Down);
        CHECK(d.newScale.value() == doctest::Approx(0.5f)); // clamped to minScale, not 0.45
    }

    SUBCASE("an up step never overshoots the ceiling")
    {
        DynResInputs in = makeInputs();
        in.appliedScale = 0.98f; // one upStep would land above maxScale
        in.framesUnderBudget = 30;
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
    // in the deadband. This replicates the caller's 3-branch counter maintenance each frame.
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

        // Caller-side deadband counter maintenance (mirrors the OffScreenViewPort tick).
        if (ema > target)          { over++;  under = 0; }
        else if (ema < target * 0.7f) { under++; over = 0; }
        else                       { over = 0; under = 0; }

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
