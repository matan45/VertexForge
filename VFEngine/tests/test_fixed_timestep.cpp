#include <doctest.h>
#include <physics/FixedTimestepMath.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstdlib>

// ============================================================
// VK-1530 — fixed-timestep accumulator + render-interpolation math.
//
// The Tests project is CPU-only and links Services, not Core/Jolt, so the
// accumulator + interpolation logic was factored into the Jolt-free header
// core/physics/FixedTimestepMath.hpp (mirroring SpatialQueryHelpers.hpp) so it
// can be exercised here. These tests pin the acceptance criteria:
//   * determinism  — identical sub-step counts regardless of frame rate,
//   * spiral guard — a hitch is clamped AND drops the remainder (alpha < 1),
//   * interpolation — alpha in [0,1], clamped so it never extrapolates.
// ============================================================

using core::physics::planFixedSteps;
using core::physics::FixedStepPlan;
using core::physics::clampAlpha;
using core::physics::interpVec3;
using core::physics::interpRotation;

namespace
{
    constexpr double TS = 1.0 / 60.0;   // 60 Hz default timestep
    constexpr double MAX_ACCUM = 0.25;  // 250 ms spiral-of-death cap
    constexpr int    MAX_STEPS = 8;

    struct RunResult { int totalSteps; double accumulator; };

    // Advance `frames` frames each by `dt`, summing the sub-steps taken.
    RunResult runFrames(double dt, int frames,
                        double timestep = TS, double maxAccum = MAX_ACCUM, int maxSteps = MAX_STEPS)
    {
        double acc = 0.0;
        int total = 0;
        for (int i = 0; i < frames; ++i)
        {
            FixedStepPlan plan = planFixedSteps(acc, dt, timestep, maxAccum, maxSteps);
            total += plan.steps;
        }
        return { total, acc };
    }
}

TEST_SUITE("FixedTimestep")
{

// ---- accumulator basics ----

TEST_CASE("one timestep of dt yields exactly one step")
{
    double acc = 0.0;
    FixedStepPlan plan = planFixedSteps(acc, TS, TS, MAX_ACCUM, MAX_STEPS);
    CHECK(plan.steps == 1);
    CHECK(plan.alpha == doctest::Approx(0.0));
    CHECK(acc == doctest::Approx(0.0));
}

TEST_CASE("sub-timestep dt accumulates without stepping, then fires one step")
{
    // timestep 0.1 for exact arithmetic
    double acc = 0.0;
    FixedStepPlan p1 = planFixedSteps(acc, 0.04, 0.1, 1.0, 8);
    CHECK(p1.steps == 0);
    CHECK(p1.alpha == doctest::Approx(0.4));

    FixedStepPlan p2 = planFixedSteps(acc, 0.04, 0.1, 1.0, 8);
    CHECK(p2.steps == 0);
    CHECK(p2.alpha == doctest::Approx(0.8));

    FixedStepPlan p3 = planFixedSteps(acc, 0.04, 0.1, 1.0, 8); // 0.12 >= 0.1 -> step
    CHECK(p3.steps == 1);
    CHECK(acc == doctest::Approx(0.02));
    CHECK(p3.alpha == doctest::Approx(0.2));
}

// ---- determinism (AC#1) ----

TEST_CASE("total steps are frame-rate independent (exact, dyadic values)")
{
    // 6.0 s of simulation delivered at three different "frame rates".
    // All dts are exact dyadic rationals so the counts are FP-exact.
    const double ts = 0.25;
    RunResult fast   = runFrames(0.125, 48, ts, 1.0, 8); // 48 * 0.125 = 6.0
    RunResult medium = runFrames(0.25,  24, ts, 1.0, 8); // 24 * 0.25  = 6.0
    RunResult slow   = runFrames(0.75,   8, ts, 1.0, 8); //  8 * 0.75  = 6.0

    CHECK(fast.totalSteps == 24);
    CHECK(medium.totalSteps == 24);
    CHECK(slow.totalSteps == 24);
}

TEST_CASE("60 Hz default: step count matches across 30/60/144 fps and conserves time")
{
    RunResult r30  = runFrames(1.0 / 30.0,  30);  // ~1 s
    RunResult r60  = runFrames(1.0 / 60.0,  60);
    RunResult r144 = runFrames(1.0 / 144.0, 144);

    // Same simulated time regardless of frame rate (±1 step at the FP boundary).
    CHECK(std::abs(r30.totalSteps - r60.totalSteps) <= 1);
    CHECK(std::abs(r60.totalSteps - r144.totalSteps) <= 1);

    // No step ever hit the per-frame cap for these dts, so time is conserved:
    // simulated (steps * timestep) + leftover == input.
    auto conserves = [](const RunResult& r, double dt, int frames)
    {
        double input = dt * static_cast<double>(frames);
        double simulated = static_cast<double>(r.totalSteps) * TS + r.accumulator;
        return std::abs(simulated - input) < 1e-6;
    };
    CHECK(conserves(r30, 1.0 / 30.0, 30));
    CHECK(conserves(r60, 1.0 / 60.0, 60));
    CHECK(conserves(r144, 1.0 / 144.0, 144));

    // Leftover accumulator is always a valid interpolation phase.
    CHECK(r30.accumulator >= 0.0);
    CHECK(r30.accumulator < TS);
    CHECK(r60.accumulator < TS);
    CHECK(r144.accumulator < TS);
}

// ---- spiral of death / drop-remainder (AC#3) ----

TEST_CASE("a 500 ms hitch is capped and drops the remainder (alpha stays < 1)")
{
    double acc = 0.0;
    FixedStepPlan plan = planFixedSteps(acc, 0.5, TS, MAX_ACCUM, MAX_STEPS);
    CHECK(plan.steps == MAX_STEPS);   // capped at 8, not ~30
    CHECK(plan.alpha >= 0.0);
    CHECK(plan.alpha < 1.0);          // remainder dropped -> interpolation, not extrapolation
    CHECK(acc < TS);
}

TEST_CASE("dt is clamped to maxAccumulator (no unbounded catch-up)")
{
    double acc = 0.0;
    FixedStepPlan plan = planFixedSteps(acc, 10.0, TS, MAX_ACCUM, MAX_STEPS);
    CHECK(plan.steps == MAX_STEPS);   // not 600 steps
    CHECK(plan.alpha < 1.0);
}

TEST_CASE("repeated hitches never spiral (alpha and accumulator stay bounded)")
{
    double acc = 0.0;
    for (int i = 0; i < 100; ++i)
    {
        FixedStepPlan plan = planFixedSteps(acc, 0.5, TS, MAX_ACCUM, MAX_STEPS);
        CHECK(plan.steps <= MAX_STEPS);
        CHECK(plan.alpha < 1.0);
        CHECK(acc < TS);
    }
}

TEST_CASE("zero / negative timestep is a safe no-op")
{
    double acc = 0.123;
    FixedStepPlan plan = planFixedSteps(acc, 0.5, 0.0, MAX_ACCUM, MAX_STEPS);
    CHECK(plan.steps == 0);
    CHECK(plan.alpha == doctest::Approx(0.0));
}

// ---- interpolation math (AC#2/#4) ----

TEST_CASE("clampAlpha clamps to [0,1]")
{
    CHECK(clampAlpha(-1.0f) == 0.0f);
    CHECK(clampAlpha(0.0f) == 0.0f);
    CHECK(clampAlpha(0.5f) == doctest::Approx(0.5f));
    CHECK(clampAlpha(1.0f) == 1.0f);
    CHECK(clampAlpha(7.0f) == 1.0f);
}

TEST_CASE("interpVec3 interpolates and never extrapolates")
{
    glm::vec3 a(0.0f);
    glm::vec3 b(10.0f, -4.0f, 2.0f);

    CHECK(interpVec3(a, b, 0.0f).x == doctest::Approx(0.0f));
    CHECK(interpVec3(a, b, 1.0f).x == doctest::Approx(10.0f));

    glm::vec3 mid = interpVec3(a, b, 0.5f);
    CHECK(mid.x == doctest::Approx(5.0f));
    CHECK(mid.y == doctest::Approx(-2.0f));
    CHECK(mid.z == doctest::Approx(1.0f));

    // overshooting / negative alpha clamps to an endpoint (no explosion on a hitch)
    CHECK(interpVec3(a, b, 7.0f).x == doctest::Approx(10.0f));
    CHECK(interpVec3(a, b, -3.0f).x == doctest::Approx(0.0f));
}

TEST_CASE("interpRotation slerps and clamps overshoot")
{
    glm::quat prev = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity
    glm::quat curr = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    CHECK(interpRotation(prev, curr, 0.0f).w == doctest::Approx(1.0f));
    CHECK(interpRotation(prev, curr, 1.0f).w == doctest::Approx(std::cos(glm::radians(45.0f))));
    CHECK(interpRotation(prev, curr, 0.5f).w == doctest::Approx(std::cos(glm::radians(22.5f))));

    // alpha 7 clamps to curr, not beyond
    CHECK(interpRotation(prev, curr, 7.0f).w == doctest::Approx(std::cos(glm::radians(45.0f))));
}

}
