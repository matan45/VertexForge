#include <doctest.h>
#include <weather/WeatherLoopGate.hpp>

#include <cmath>

// ============================================================
// VK-1521: the weather loop start/stop gate (utilities/weather/WeatherLoopGate.hpp).
//
// A weather loop is a stream whose release is IRREVERSIBLE for 2s (fadeOutLoop arms a
// FadeOutAndReleaseSoundCommand and drops the handle). So a target that dips below the
// silence threshold for one frame and recovers used to leave a fading stream behind while
// the start gate immediately minted a second one — two copies of the same loop, at
// different offsets, on the Weather bus.
//
// Before VK-1521 the stop gate watched the smoothed follower instead of the raw target, so
// the follower's decay was an accidental debounce. These pin the explicit replacement.
//
// CPU-only, and header-only by necessity: Tests does not link Weather (premake5.lua), but
// does carry VFEngine/utilities on its include path.
// ============================================================

namespace
{
    using namespace weather;

    constexpr float kRampSpeed = 1.5f;  // WeatherAudioController::VOLUME_RAMP_SPEED
    constexpr float kRainMax = 0.7f;    // WeatherAudioController::RAIN_MAX_VOLUME
    constexpr float kFrame = 1.0f / 60.0f;

    // Drives the gate the way WeatherAudioController does, tracking the handle for us.
    struct Rig
    {
        LoopGateState state;
        bool hasHandle = false;
        int starts = 0;
        int stops = 0;

        void step(float target, float dt = kFrame)
        {
            const LoopGateDecision d =
                evaluateLoopGate(state, target, dt, hasHandle, /*hasPath=*/true, kRampSpeed);
            if (d.start)
            {
                ++starts;
                hasHandle = true;
            }
            if (d.stop)
            {
                ++stops;
                hasHandle = false; // fadeOutLoop zeroes the handle — the stream is gone
            }
        }

        void hold(float target, float seconds)
        {
            for (float t = 0.0f; t < seconds; t += kFrame)
                step(target);
        }
    };
}

TEST_SUITE("WeatherLoopGate")
{
    TEST_CASE("a single-frame dip does not release the loop")
    {
        // THE regression. At HEAD the raw-target gate fires a stop on the dip frame, zeroes
        // the handle, and the very next frame starts a SECOND stream while the first still
        // has ~1.97s of uncancellable fade left.
        Rig r;
        r.hold(0.5f * kRainMax, 1.0f);
        REQUIRE(r.starts == 1);
        REQUIRE(r.stops == 0);

        r.step(0.0f);            // one 16ms frame of silence
        r.hold(0.5f * kRainMax, 0.5f); // and it is back

        CHECK(r.stops == 0);  // HEAD: 1
        CHECK(r.starts == 1); // HEAD: 2 — the stacked stream
    }

    TEST_CASE("sustained silence still releases the loop, exactly once")
    {
        Rig r;
        r.hold(0.5f * kRainMax, 1.0f);
        REQUIRE(r.starts == 1);

        r.hold(0.0f, LOOP_STOP_HOLD_SECONDS + 0.1f);
        CHECK(r.stops == 1);

        // Still silent: the handle is gone, so there is nothing left to stop again.
        r.hold(0.0f, 1.0f);
        CHECK(r.stops == 1);
        CHECK(r.starts == 1);
    }

    TEST_CASE("the hold covers the loudest debounce the pre-VK-1521 follower gave")
    {
        // dev's stop gate waited for the FOLLOWER to decay to 0.01 at VOLUME_RAMP_SPEED, so
        // its debounce was volume-dependent: (v - 0.01) / 1.5. Rain at RAIN_MAX_VOLUME is
        // the worst case in the engine, and the explicit hold must not be shorter than it or
        // content that was stable on dev would start stacking.
        const float devHoldAtFullRain = (kRainMax - 0.01f) / kRampSpeed;
        CHECK(devHoldAtFullRain == doctest::Approx(0.46f).epsilon(0.01));
        CHECK(LOOP_STOP_HOLD_SECONDS >= devHoldAtFullRain);

        // A dip shorter than dev's own debounce must survive — this is the parity claim.
        Rig r;
        r.hold(kRainMax, 1.0f);
        r.hold(0.0f, devHoldAtFullRain - 0.05f);
        r.hold(kRainMax, 0.2f);
        CHECK(r.stops == 0);
        CHECK(r.starts == 1);
    }

    TEST_CASE("silence is measured continuously, not accumulated across recoveries")
    {
        // Two dips that each sit under the hold must not add up to a stop between them.
        Rig r;
        r.hold(0.5f * kRainMax, 0.5f);
        for (int i = 0; i < 4; ++i)
        {
            r.hold(0.0f, LOOP_STOP_HOLD_SECONDS * 0.6f);
            r.hold(0.5f * kRainMax, 0.1f);
        }
        CHECK(r.stops == 0);
        CHECK(r.starts == 1);
    }

    TEST_CASE("wind never stops, so its trajectory is untouched by the hold")
    {
        // windTarget = WIND_BASE_VOLUME + ... is always >= 0.1, so wantsLoop is always true
        // and the gate is literally the pre-existing start + follower path. This is the
        // behaviour-preservation claim for the only loop that is always audible.
        Rig r;
        r.step(0.1f);
        REQUIRE(r.starts == 1);
        CHECK(r.state.volume == doctest::Approx(0.1f)); // seeded AT target, not ramped from 0

        r.hold(0.4f, 2.0f);
        CHECK(r.stops == 0);
        CHECK(r.state.volume == doctest::Approx(0.4f)); // follower converged
        CHECK(r.state.silentTime == doctest::Approx(0.0f));
    }

    TEST_CASE("the follower is frozen while silent, never ramped toward zero")
    {
        // Load-bearing: StreamingAudioManager::startFadeOut captures baseVolume by reading
        // back the live AL_GAIN, so decaying the follower first would base the 2s tail on a
        // near-zero gain and make the fade inaudible.
        Rig r;
        r.hold(0.35f, 1.0f);
        const float before = r.state.volume;
        REQUIRE(before == doctest::Approx(0.35f));

        r.hold(0.0f, LOOP_STOP_HOLD_SECONDS * 0.5f);
        CHECK(r.state.volume == doctest::Approx(before)); // frozen, not decayed
        CHECK(r.stops == 0);
    }

    TEST_CASE("KNOWN RESIDUAL: a dip longer than the hold still stacks a second stream")
    {
        // Pinned deliberately so nobody mistakes the debounce for a full fix. Releasing is
        // irreversible for CROSSFADE_DURATION_MS (2s), but the handle is dropped
        // immediately, so any recovery inside that window starts a second stream on top of
        // the first one's tail.
        //
        // The real fix needs two things this gate cannot reach: a fade-IN reversal in
        // StreamingAudioManager::startFadeIn (which currently hard-returns when a ramp is
        // already in flight), and Weather retaining the fading handle instead of zeroing it.
        // Follow-up ticket; the hold only bounds the exposure to dips > 0.5s.
        Rig r;
        r.hold(0.5f * kRainMax, 1.0f);
        REQUIRE(r.starts == 1);

        r.hold(0.0f, LOOP_STOP_HOLD_SECONDS + 0.1f);
        REQUIRE(r.stops == 1); // committed to a 2s fade

        r.hold(0.5f * kRainMax, 0.1f);
        CHECK(r.starts == 2); // <- the second stream, while the first is still fading
    }
}
