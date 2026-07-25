// VK-1607 review findings #6: WaterWakeEmitterComponent's emit decision.
//
// utilities/water/WakeMath.hpp is pure math - no registry, no ripple sim, no Vulkan - specifically so
// the regression below can be pinned down here rather than only in a running scene.

#include <doctest.h>

#include <water/WakeMath.hpp>

#include <algorithm>
#include <limits>

TEST_SUITE("WakeEmitter")
{
    TEST_CASE("frame speed is measured over one tick")
    {
        CHECK(water::wakeFrameSpeed({1.0f, 0.0f}, {0.0f, 0.0f}, 1.0f) == doctest::Approx(1.0f));
        CHECK(water::wakeFrameSpeed({0.0f, 3.0f}, {0.0f, 0.0f}, 0.5f) == doctest::Approx(6.0f));
        CHECK(water::wakeFrameSpeed({0.0f, 0.0f}, {0.0f, 0.0f}, 1.0f / 60.0f) == doctest::Approx(0.0f));

        SUBCASE("a non-positive dt never divides")
        {
            CHECK(water::wakeFrameSpeed({10.0f, 0.0f}, {0.0f, 0.0f}, 0.0f) == 0.0f);
            CHECK(water::wakeFrameSpeed({10.0f, 0.0f}, {0.0f, 0.0f}, -1.0f) == 0.0f);
        }
    }

    TEST_CASE("the minimum speed gate cannot be defeated by accumulation")
    {
        // THE REGRESSION. The stored anchor used to move only when an impulse was queued, so
        // `travelled` accrued across every rejected frame while dt stayed one frame long. A drifter
        // at 0.05 m/s under a 0.5 m/s minimum eventually accrued 0.5 m and reported
        // 0.5 / (1/60) = 30 m/s - a full-strength ring from something barely moving.
        constexpr float dt = 1.0f / 60.0f;
        constexpr float driftSpeed = 0.05f;          // m/s, well under minSpeed
        constexpr float minSpeed = 0.5f;
        constexpr float travelInterval = 0.5f;
        const float step = driftSpeed * dt;          // metres per tick

        water::WakeTrailState trail{};
        glm::vec2 pos{0.0f, 0.0f};
        int emits = 0;

        // 30 seconds of drifting - six times longer than the old bug needed to fire.
        for (int i = 0; i < 1800; ++i)
        {
            pos.x += step;

            const float speed = water::wakeFrameSpeed(pos, trail.lastPositionXZ, dt);
            const float travelledSinceEmit = glm::length(pos - trail.lastEmitXZ);
            trail.lastPositionXZ = pos;   // the fix: EVERY tick, not only on emit

            if (water::wakeShouldEmit(speed, travelledSinceEmit, minSpeed, travelInterval, false))
            {
                ++emits;
                trail.lastEmitXZ = pos;
            }
        }

        CHECK(emits == 0);
        // ...even though it has covered far more than the travel interval by now.
        CHECK(glm::length(pos - glm::vec2(0.0f)) > travelInterval);
    }

    TEST_CASE("above the minimum speed the travel interval sets the ring spacing")
    {
        constexpr float dt = 1.0f / 60.0f;
        constexpr float speed = 6.0f;                // m/s, comfortably over the minimum
        constexpr float travelInterval = 0.5f;
        const float step = speed * dt;               // 0.1 m per tick

        water::WakeTrailState trail{};
        glm::vec2 pos{0.0f, 0.0f};
        int emits = 0;
        float lastEmitX = 0.0f;
        float minGap = std::numeric_limits<float>::max();
        bool haveEmitted = false;

        for (int i = 0; i < 600; ++i)                // 10 s -> 60 m travelled
        {
            pos.x += step;

            const float s = water::wakeFrameSpeed(pos, trail.lastPositionXZ, dt);
            const float travelledSinceEmit = glm::length(pos - trail.lastEmitXZ);
            trail.lastPositionXZ = pos;

            if (water::wakeShouldEmit(s, travelledSinceEmit, 0.5f, travelInterval, false))
            {
                if (haveEmitted)
                    minGap = std::min(minGap, pos.x - lastEmitX);
                haveEmitted = true;
                lastEmitX = pos.x;
                ++emits;
                trail.lastEmitXZ = pos;
            }
        }

        // The contract: rings are never closer together than the authored interval.
        REQUIRE(haveEmitted);
        CHECK(minGap >= travelInterval - 1.0e-3f);

        // 60 m at one ring every 0.5 m is ~120 rings, and the count is deliberately checked as a
        // BAND rather than a number. The gate compares a float difference of two accumulated world
        // coordinates against the interval, and at ~60 m from the origin one ULP is ~4e-6 m - so a
        // tick can land a hair short of 0.5 m and push that ring to the next tick, stretching the
        // occasional gap to 0.6 m. That is a property of distance throttling in float, not of this
        // gate; the band still fails loudly if the gate emitted every tick (600) or never (0).
        CHECK(emits >= 100);
        CHECK(emits <= 120);
    }

    TEST_CASE("continuous ignores the travel interval but not the minimum speed")
    {
        CHECK(water::wakeShouldEmit(10.0f, 0.0f, 0.5f, 0.5f, true));
        CHECK(water::wakeShouldEmit(0.5f, 0.0f, 0.5f, 0.5f, true));    // exactly at the threshold
        CHECK_FALSE(water::wakeShouldEmit(0.49f, 100.0f, 0.5f, 0.5f, true));
    }

    TEST_CASE("a zero minimum speed still respects the spacing")
    {
        // An emitter authored with minSpeed 0 emits whenever it has travelled far enough, and never
        // when it is standing still with continuous off.
        CHECK_FALSE(water::wakeShouldEmit(0.0f, 0.0f, 0.0f, 0.5f, false));
        CHECK(water::wakeShouldEmit(0.0f, 0.5f, 0.0f, 0.5f, false));
        CHECK(water::wakeShouldEmit(0.0f, 0.0f, 0.0f, 0.5f, true));
    }
}
