// CPU-only coverage for VK-1536 (script tick governor).
//
//   1. Opt-in contract: interval 0 (the default) is byte-identical to the pre-VK-1536 loop —
//      every frame, tickDt == frame dt. A negative / NaN / Inf interval fails OPEN the same way.
//   2. dt accumulation (the ticket's explicit acceptance criterion): a throttled script is handed
//      the REAL elapsed time since its own last tick, so sum(tickDt) equals total elapsed game time
//      with no drift and a throttled script integrates to the same place as an unthrottled one.
//   3. Stagger: N scripts sharing an interval must NOT all fire on the same frame — that would turn
//      a steady per-frame cost into an Nx sawtooth spike and defeat the whole feature.
//   4. Distance/significance scaling: the buckets are significance-score thresholds in distance
//      form, disabled by default, and can only ever slow a script down.
//
// No registry, no mType interpreter, no Vulkan device — the governor is a header-only pure core.

#include <doctest.h>

#include <scripting/ScriptTickGovernor.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
    constexpr float kFrame60 = 1.0f / 60.0f;

    // Drive one script for `frames` frames at a fixed dt, returning every tickDt it was handed.
    std::vector<float> runFrames(float interval, float dt, int frames, uint64_t instanceId = 1)
    {
        std::vector<float> ticks;
        scripting::ScriptTickInputs in;
        in.interval = interval;
        in.dt = dt;
        in.instanceId = instanceId;

        for (int i = 0; i < frames; ++i)
        {
            const auto d = scripting::evaluateScriptTick(in);
            in.accumulator = d.accumulator;
            in.firstTickDone = d.firstTickDone;
            if (d.shouldTick)
                ticks.push_back(d.tickDt);
        }
        return ticks;
    }
}

TEST_SUITE("ScriptTickGovernorOptIn")
{
    TEST_CASE("interval 0 ticks every frame and forwards the frame dt unchanged")
    {
        const auto ticks = runFrames(0.0f, kFrame60, 100);
        REQUIRE(ticks.size() == 100);
        for (const float t : ticks)
            CHECK(t == doctest::Approx(kFrame60));
    }

    TEST_CASE("a non-positive or non-finite interval fails OPEN (ticks every frame)")
    {
        // The failure mode of a garbage value must be "runs as it does today", never
        // "silently stops running".
        for (const float bad : {-1.0f, -0.0f,
                                std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity()})
        {
            const auto ticks = runFrames(bad, kFrame60, 30);
            CHECK(ticks.size() == 30);
        }
    }

    TEST_CASE("an interval shorter than the frame degrades to every-frame")
    {
        const auto ticks = runFrames(0.001f, kFrame60, 50);
        CHECK(ticks.size() == 50);
    }
}

TEST_SUITE("ScriptTickGovernorAccumulation")
{
    TEST_CASE("no drift: no game time is ever lost or double-counted")
    {
        // The AC's core requirement. The governor drops the accumulator remainder on tick, which is
        // only safe BECAUSE tickDt carries the real elapsed time. The exact invariant is therefore
        // "delivered + still-pending == total elapsed" — not "delivered == total", since at any
        // instant up to one interval of time is legitimately sitting in the accumulator waiting for
        // the next tick.
        const int frames = 1000;

        scripting::ScriptTickInputs in;
        in.interval = 0.1f;
        in.dt = kFrame60;
        in.instanceId = 1;

        float delivered = 0.0f;
        int tickCount = 0;
        for (int i = 0; i < frames; ++i)
        {
            const auto d = scripting::evaluateScriptTick(in);
            in.accumulator = d.accumulator;
            in.firstTickDone = d.firstTickDone;
            if (d.shouldTick)
            {
                delivered += d.tickDt;
                ++tickCount;
            }
        }

        CHECK(delivered + in.accumulator == doctest::Approx(frames * kFrame60).epsilon(0.0001f));
        CHECK(in.accumulator < 0.1f); // the pending tail never exceeds one interval
        // ~16.67s of game time at 10Hz => ~166 ticks, +/-1 for the staggered first deadline.
        CHECK(tickCount >= 160);
        CHECK(tickCount <= 168);
    }

    TEST_CASE("a throttled script integrates to the same place, lagging at most one interval")
    {
        // The behavioral promise made to script authors: integrate against the passed dt and
        // throttling costs you nothing but latency. It must never over-integrate (which would make
        // a unit outrun its unthrottled twin) and must never lag by more than the pending tail.
        const int frames = 600;
        const float speed = 3.0f;
        const float interval = 0.1f;

        float throttledPos = 0.0f;
        for (const float t : runFrames(interval, kFrame60, frames))
            throttledPos += speed * t;

        float fullRatePos = 0.0f;
        for (const float t : runFrames(0.0f, kFrame60, frames))
            fullRatePos += speed * t;

        CHECK(throttledPos <= fullRatePos);                              // never overshoots
        CHECK(fullRatePos - throttledPos <= speed * interval + 1e-3f);   // lag is bounded
    }

    TEST_CASE("tickDt is the elapsed time since the last tick, not the frame dt")
    {
        // 0.1s at 60Hz => ~6 frames of accumulation per tick.
        const auto ticks = runFrames(0.1f, kFrame60, 300);
        REQUIRE(ticks.size() > 2);
        // Skip [0]: the first deadline is deliberately staggered short.
        for (size_t i = 1; i < ticks.size(); ++i)
        {
            CHECK(ticks[i] >= 0.1f);
            CHECK(ticks[i] < 0.1f + kFrame60);
        }
    }

    TEST_CASE("a hitch fires exactly once with the whole elapsed time, no catch-up burst")
    {
        scripting::ScriptTickInputs in;
        in.interval = 0.1f;
        in.dt = 2.5f; // a 2.5-second stall — 25 intervals' worth
        in.firstTickDone = true;

        const auto d = scripting::evaluateScriptTick(in);
        CHECK(d.shouldTick);
        CHECK(d.tickDt == doctest::Approx(2.5f));
        CHECK(d.accumulator == doctest::Approx(0.0f));
    }

    TEST_CASE("a paused sim (dt 0) never ticks and never accumulates")
    {
        // dt is game time, so pause must freeze the cadence rather than bank up a burst on resume.
        const auto ticks = runFrames(0.1f, 0.0f, 100);
        CHECK(ticks.empty());
    }
}

TEST_SUITE("ScriptTickGovernorStagger")
{
    TEST_CASE("200 scripts sharing an interval never all fire on the same frame")
    {
        // The anti-spike guarantee. Without the staggered first deadline these accumulate in
        // lockstep and every one of them fires on the same frame every 0.1s — a 200x spike that
        // would make the governor worse than doing nothing.
        constexpr int kScripts = 200;
        constexpr int kFrames = 600;
        const float interval = 0.1f;

        std::vector<scripting::ScriptTickInputs> state(kScripts);
        for (int i = 0; i < kScripts; ++i)
        {
            state[i].interval = interval;
            state[i].dt = kFrame60;
            state[i].instanceId = static_cast<uint64_t>(i) + 1;
        }

        int worstFrame = 0;
        int totalTicks = 0;
        for (int f = 0; f < kFrames; ++f)
        {
            int firedThisFrame = 0;
            for (auto& s : state)
            {
                const auto d = scripting::evaluateScriptTick(s);
                s.accumulator = d.accumulator;
                s.firstTickDone = d.firstTickDone;
                if (d.shouldTick)
                {
                    ++firedThisFrame;
                    ++totalTicks;
                }
            }
            worstFrame = std::max(worstFrame, firedThisFrame);
        }

        // Mean load is 200 scripts / 6 frames-per-interval ~= 33 per frame. Allow generous headroom
        // for hash clustering, but pin that the pathological all-at-once case cannot happen.
        CHECK(worstFrame < kScripts / 2);
        CHECK(totalTicks > 0);
    }

    TEST_CASE("unitHash is deterministic and in [0, 1)")
    {
        for (uint64_t i = 0; i < 1000; ++i)
        {
            const float h = scripting::unitHash(i);
            CHECK(h >= 0.0f);
            CHECK(h < 1.0f);
            CHECK(h == scripting::unitHash(i)); // stable across calls
        }
    }

    TEST_CASE("distinct instances get distinct phases")
    {
        // If the hash collapsed, the stagger would silently stop working.
        int distinct = 0;
        const float a = scripting::unitHash(1);
        for (uint64_t i = 2; i < 100; ++i)
        {
            if (scripting::unitHash(i) != a)
                ++distinct;
        }
        CHECK(distinct > 90);
    }
}
