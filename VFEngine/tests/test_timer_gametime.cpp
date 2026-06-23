#include <doctest.h>

#include <time/Timer.hpp>

// Phase 2 (Play/Pause/Stop): gameplay-delta computation. These tests target the
// pure helper Timer::computeGameDelta() so they are deterministic - Timer::update()
// itself reads the wall clock and cannot be driven reproducibly here.

using engineTime::Timer;

TEST_CASE("Timer gameplay delta computation")
{
    SUBCASE("running scales raw delta")
    {
        bool step = false;
        // Not paused: gameDelta = raw * scale, regardless of the step flag.
        CHECK(Timer::computeGameDelta(0.016, 1.0, /*paused=*/false, step) == doctest::Approx(0.016));
        CHECK(Timer::computeGameDelta(0.016, 2.0, /*paused=*/false, step) == doctest::Approx(0.032));
        CHECK(Timer::computeGameDelta(0.016, 0.25, /*paused=*/false, step) == doctest::Approx(0.004));
        // Step flag is irrelevant while running and must be left untouched.
        CHECK(step == false);
    }

    SUBCASE("scale 0 while running yields zero (gameplay inactive that frame)")
    {
        bool step = false;
        double d = Timer::computeGameDelta(0.016, 0.0, /*paused=*/false, step);
        // raw * 0 == 0, so isGameTimeActive() (delta > 0) would report inactive,
        // which is what unifies the pause / step / scale==0 gate.
        CHECK(d == doctest::Approx(0.0));
        CHECK(step == false);
    }

    SUBCASE("paused without step yields zero")
    {
        bool step = false;
        double d = Timer::computeGameDelta(0.016, 1.0, /*paused=*/true, step);
        CHECK(d == doctest::Approx(0.0));
        CHECK(step == false);
    }

    SUBCASE("paused with step yields one fixed frame and clears the request")
    {
        bool step = true;
        double d = Timer::computeGameDelta(0.5, 1.0, /*paused=*/true, step);
        CHECK(d == doctest::Approx(1.0 / 60.0));
        // The step request is consumed exactly once.
        CHECK(step == false);

        // A subsequent paused frame with no new request is zero again.
        double d2 = Timer::computeGameDelta(0.5, 1.0, /*paused=*/true, step);
        CHECK(d2 == doctest::Approx(0.0));
    }

    SUBCASE("paused step honors the time scale")
    {
        bool step = true;
        double d = Timer::computeGameDelta(0.016, 2.0, /*paused=*/true, step);
        CHECK(d == doctest::Approx((1.0 / 60.0) * 2.0));
        CHECK(step == false);
    }
}

TEST_CASE("Timer isGameTimeActive reflects the current game delta")
{
    // resetGameTime() forces gameDeltaTime back to 0 -> inactive. This exercises the
    // public accessor path without touching the wall-clock-driven update().
    Timer::resetGameTime();
    CHECK(Timer::isGameTimeActive() == false);
    CHECK(Timer::getGameDeltaTime() == doctest::Approx(0.0));

    // Defaults after reset: unpaused, 1x.
    Timer::setGamePaused(false);
    Timer::setTimeScale(1.0);
    // State setters do not by themselves recompute the delta (that happens in
    // update()); resetGameTime keeps the accessor at the inactive baseline.
    CHECK(Timer::isGameTimeActive() == false);
}
