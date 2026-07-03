#include <doctest.h>
#include <render/raytracing/RTShadowBudget.hpp>

#include <vector>

// ============================================================
// evaluateBudgetCore — the pure adaptive RT-shadow budget decision extracted from
// RTShadowProfiler (VK-1479 C5). No Vulkan, so the CPU-only Tests project pins down:
//   * the hysteresis thresholds (nothing happens until framesOver/Under reach the limits),
//   * the degrade order  : spatial passes -> ray distance -> spot budget -> point budget -> skip,
//   * the upgrade order  : unskip -> point budget -> spot budget -> ray distance -> spatial passes,
//   * directional-only parity: with spot/point inactive the order collapses to the pre-C5
//     spatial -> ray -> skip / unskip -> ray -> spatial ordering (byte-identical behavior).
// ============================================================

using render::raytracing::BudgetInputs;
using render::raytracing::BudgetDecision;
using render::raytracing::evaluateBudgetCore;

namespace
{
    // A fully-degradable, spot+point-active baseline. Callers tweak fields per case.
    BudgetInputs makeInputs()
    {
        BudgetInputs in{};
        in.adaptiveEnabled = true;
        in.emaInitialized = true;
        in.emaTotalMs = 5.0f; // over budget (unused by the decision except via the counters)
        in.budgetMs = 2.0f;
        in.restoreThreshold = 0.7f;
        in.framesOverBudget = 0;
        in.framesUnderBudget = 0;
        in.hysteresisFramesDown = 10;
        in.hysteresisFramesUp = 30;
        in.baseMaxRayDistance = 500.0f;
        in.appliedMaxRayDistance = 500.0f;
        in.baseSpatialPasses = 3;
        in.appliedSpatialPasses = 3;
        in.spotActive = true;
        in.baseSpotBudget = 8;
        in.appliedSpotBudget = 8;
        in.pointActive = true;
        in.basePointBudget = 8;
        in.appliedPointBudget = 8;
        in.skipActive = false;
        return in;
    }

    enum class Step { None, Spatial, Ray, Spot, Point, Skip };

    Step classify(const BudgetDecision& d)
    {
        if (d.newSpatialPasses.has_value()) return Step::Spatial;
        if (d.newMaxRayDistance.has_value()) return Step::Ray;
        if (d.newSpotBudget.has_value()) return Step::Spot;
        if (d.newPointBudget.has_value()) return Step::Point;
        if (d.skipFrame) return Step::Skip;
        return Step::None;
    }
}

TEST_CASE("evaluateBudgetCore: guards — no adaptation when disabled or not initialized")
{
    SUBCASE("adaptive disabled leaves everything untouched")
    {
        BudgetInputs in = makeInputs();
        in.adaptiveEnabled = false;
        in.framesOverBudget = 999; // would degrade if it were enabled
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::None);
        CHECK(d.appliedSpatialPasses == 3);
        CHECK(d.appliedMaxRayDistance == doctest::Approx(500.0f));
        // Guard must not consume the over-budget streak.
        CHECK(d.framesOverBudget == 999u);
    }

    SUBCASE("uninitialized EMA leaves everything untouched")
    {
        BudgetInputs in = makeInputs();
        in.emaInitialized = false;
        in.framesOverBudget = 999;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::None);
        CHECK(d.framesOverBudget == 999u);
    }
}

TEST_CASE("evaluateBudgetCore: hysteresis thresholds gate the action")
{
    SUBCASE("one frame short of the down threshold does nothing")
    {
        BudgetInputs in = makeInputs();
        in.framesOverBudget = 9; // hysteresisFramesDown - 1
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::None);
        CHECK(d.framesOverBudget == 9u); // not reset — streak continues
    }

    SUBCASE("reaching the down threshold degrades and resets the streak")
    {
        BudgetInputs in = makeInputs();
        in.framesOverBudget = 10;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Spatial);
        CHECK(d.framesOverBudget == 0u);
    }

    SUBCASE("one frame short of the up threshold does nothing")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1; // already degraded so a restore would be available
        in.appliedMaxRayDistance = 300.0f;
        in.framesUnderBudget = 29; // hysteresisFramesUp - 1
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::None);
        CHECK(d.framesUnderBudget == 29u);
    }

    SUBCASE("reaching the up threshold restores and resets the streak")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1;
        in.appliedMaxRayDistance = 300.0f;
        in.appliedSpotBudget = 8;   // spot/point already at base so the ray lever is next
        in.appliedPointBudget = 8;
        in.framesUnderBudget = 30;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Ray); // reverse order: budgets full -> restore ray distance
        CHECK(d.framesUnderBudget == 0u);
    }
}

TEST_CASE("evaluateBudgetCore: degrade order is spatial -> ray -> spot -> point -> skip")
{
    // Each sub-case pins one boundary by exhausting the earlier levers.
    SUBCASE("spatial passes degrade first")
    {
        BudgetInputs in = makeInputs();
        in.framesOverBudget = 10;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Spatial);
        CHECK(d.newSpatialPasses.value() == 2);
        CHECK(d.appliedSpatialPasses == 2);
        CHECK_FALSE(d.newMaxRayDistance.has_value());
    }

    SUBCASE("ray distance next once spatial passes hit the floor")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1; // floor
        in.framesOverBudget = 10;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Ray);
        CHECK(d.newMaxRayDistance.value() == doctest::Approx(375.0f)); // 500 * 0.75
    }

    SUBCASE("spot budget next once ray distance hits the floor")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1;
        in.appliedMaxRayDistance = 125.0f; // == base * 0.25, not > floor
        in.framesOverBudget = 10;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Spot);
        CHECK(d.newSpotBudget.value() == 4u); // 8 / 2
    }

    SUBCASE("point budget next once spot budget hits the floor")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1;
        in.appliedMaxRayDistance = 125.0f;
        in.appliedSpotBudget = 1; // floor
        in.framesOverBudget = 10;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Point);
        CHECK(d.newPointBudget.value() == 4u);
    }

    SUBCASE("skip once every lever is exhausted")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1;
        in.appliedMaxRayDistance = 125.0f;
        in.appliedSpotBudget = 1;
        in.appliedPointBudget = 1;
        in.framesOverBudget = 10;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Skip);
        CHECK(d.skipFrame);
        CHECK(d.skipActive); // skip state latches on the skip frame
    }
}

TEST_CASE("evaluateBudgetCore: spot/point steps only apply when active")
{
    SUBCASE("inactive spot budget is not degraded — falls through to point")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1;
        in.appliedMaxRayDistance = 125.0f;
        in.spotActive = false; // spot RT off — step skipped
        in.framesOverBudget = 10;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Point);
    }

    SUBCASE("directional-only setup skips both spot and point and goes straight to skip")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1;
        in.appliedMaxRayDistance = 125.0f;
        in.spotActive = false;
        in.pointActive = false;
        in.framesOverBudget = 10;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Skip);
    }
}

TEST_CASE("evaluateBudgetCore: upgrade order is the reverse — unskip -> point -> spot -> ray -> spatial")
{
    SUBCASE("unskip takes precedence over every budget restore")
    {
        BudgetInputs in = makeInputs();
        in.skipActive = true;
        in.appliedSpatialPasses = 1;
        in.appliedMaxRayDistance = 125.0f;
        in.appliedSpotBudget = 1;
        in.appliedPointBudget = 1;
        in.framesUnderBudget = 30;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::None); // no quality lever raised this cycle
        CHECK_FALSE(d.skipFrame);
        CHECK_FALSE(d.skipActive); // skip state cleared
        CHECK(d.framesUnderBudget == 0u);
    }

    SUBCASE("point budget restored first (once not skipping)")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1;
        in.appliedMaxRayDistance = 125.0f;
        in.appliedSpotBudget = 1;
        in.appliedPointBudget = 1;
        in.framesUnderBudget = 30;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Point);
        CHECK(d.newPointBudget.value() == 2u); // 1 -> 2
    }

    SUBCASE("spot budget restored after point reaches base")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1;
        in.appliedMaxRayDistance = 125.0f;
        in.appliedSpotBudget = 1;
        in.appliedPointBudget = 8; // at base
        in.framesUnderBudget = 30;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Spot);
        CHECK(d.newSpotBudget.value() == 2u);
    }

    SUBCASE("spatial passes restored last")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpatialPasses = 1;
        in.appliedMaxRayDistance = 500.0f; // at base
        in.appliedSpotBudget = 8;
        in.appliedPointBudget = 8;
        in.framesUnderBudget = 30;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::Spatial);
        CHECK(d.newSpatialPasses.value() == 2);
    }
}

TEST_CASE("evaluateBudgetCore: throttled flag reflects any reduced lever")
{
    SUBCASE("at base with no change -> not throttled")
    {
        BudgetInputs in = makeInputs();
        in.framesOverBudget = 5; // below threshold, no action
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK_FALSE(d.throttled);
    }

    SUBCASE("a spatial degrade marks throttled")
    {
        BudgetInputs in = makeInputs();
        in.framesOverBudget = 10;
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(d.throttled);
    }

    SUBCASE("a shrunk spot budget marks throttled even at base directional levers")
    {
        BudgetInputs in = makeInputs();
        in.appliedSpotBudget = 4; // already shrunk from base 8
        in.framesOverBudget = 5;  // no new action this call
        BudgetDecision d = evaluateBudgetCore(in);
        CHECK(classify(d) == Step::None);
        CHECK(d.throttled);
    }
}

TEST_CASE("evaluateBudgetCore: full degrade sweep keeps the strict lever ordering")
{
    // Drive repeated over-budget degrades, feeding the applied state back each cycle, and confirm
    // the sequence of levers is non-decreasing in [Spatial < Ray < Spot < Point < Skip] and that
    // every lever is exercised. This guards the whole ordering, not just adjacent boundaries.
    BudgetInputs in = makeInputs();
    std::vector<Step> order;
    for (int i = 0; i < 40; ++i)
    {
        in.framesOverBudget = in.hysteresisFramesDown;
        BudgetDecision d = evaluateBudgetCore(in);
        Step s = classify(d);
        order.push_back(s);
        // Feed applied state forward.
        in.appliedSpatialPasses = d.appliedSpatialPasses;
        in.appliedMaxRayDistance = d.appliedMaxRayDistance;
        in.appliedSpotBudget = d.appliedSpotBudget;
        in.appliedPointBudget = d.appliedPointBudget;
        in.skipActive = d.skipActive;
    }

    auto rank = [](Step s) { return static_cast<int>(s); };
    bool sawSpatial = false, sawRay = false, sawSpot = false, sawPoint = false, sawSkip = false;
    Step prev = Step::Spatial;
    for (Step s : order)
    {
        REQUIRE(s != Step::None); // always over budget => always acts
        CHECK(rank(s) >= rank(prev)); // never regresses to an earlier lever
        prev = s;
        sawSpatial = sawSpatial || s == Step::Spatial;
        sawRay = sawRay || s == Step::Ray;
        sawSpot = sawSpot || s == Step::Spot;
        sawPoint = sawPoint || s == Step::Point;
        sawSkip = sawSkip || s == Step::Skip;
    }
    CHECK(sawSpatial);
    CHECK(sawRay);
    CHECK(sawSpot);
    CHECK(sawPoint);
    CHECK(sawSkip);
}
