#include <doctest.h>
#include <water/SwimMath.hpp>

#include <cmath>

// ==================================================================================
// VK-1606: character-controller swimming (CPU only - no GPU twin)
// ==================================================================================

TEST_SUITE("SwimMath") {

TEST_CASE("capsule submersion is a clamped fraction of the capsule") {
    const float halfHeight = 0.9f;           // a 1.8 m character

    CHECK(water::capsuleSubmersion(0.0f, halfHeight, -0.9f) == doctest::Approx(0.0f));   // surface at the feet
    CHECK(water::capsuleSubmersion(0.0f, halfHeight, 0.9f) == doctest::Approx(1.0f));    // surface at the head
    CHECK(water::capsuleSubmersion(0.0f, halfHeight, 0.0f) == doctest::Approx(0.5f));    // waist deep

    SUBCASE("it never leaves [0, 1]") {
        for (float w : {-100.0f, -1.0f, 0.3f, 1.0f, 100.0f})
        {
            const float s = water::capsuleSubmersion(0.0f, halfHeight, w);
            CHECK(s >= 0.0f);
            CHECK(s <= 1.0f);
        }
        CHECK(water::capsuleSubmersion(0.0f, 0.0f, 5.0f) == doctest::Approx(1.0f));      // degenerate capsule
    }
}

TEST_CASE("the float target really produces the requested submersion") {
    // Round trip: park the capsule at swimTargetCenterY and the submersion must come back out as
    // floatDepth / capsuleHeight. If these two drift apart, characters settle at the wrong depth.
    const float halfHeight = 0.9f;
    const float waterHeight = 12.5f;

    for (float floatDepth : {0.0f, 0.45f, 1.2f, 1.8f})
    {
        const float y = water::swimTargetCenterY(waterHeight, halfHeight, floatDepth);
        CHECK(water::capsuleSubmersion(y, halfHeight, waterHeight) ==
              doctest::Approx(floatDepth / (2.0f * halfHeight)));
    }

    SUBCASE("floatDepth is clamped to the capsule") {
        // Asking for 10 m of submersion on a 1.8 m character must fully submerge it, not launch it.
        const float y = water::swimTargetCenterY(waterHeight, halfHeight, 10.0f);
        CHECK(y == doctest::Approx(waterHeight - halfHeight));
        CHECK(water::capsuleSubmersion(y, halfHeight, waterHeight) == doctest::Approx(1.0f));

        const float dry = water::swimTargetCenterY(waterHeight, halfHeight, -5.0f);
        CHECK(dry == doctest::Approx(waterHeight + halfHeight));
    }
}

TEST_CASE("buoyancy spring is at rest exactly at the float depth") {
    const float target = 3.25f;

    // The defining property: no force, no drift, no jitter when already floating.
    CHECK(water::buoyancyStep(target, target, 0.0f, 40.0f, 1.0f / 60.0f) == 0.0f);

    // Below the target it pushes up, above it pulls down.
    CHECK(water::buoyancyStep(target - 1.0f, target, 0.0f, 40.0f, 1.0f / 60.0f) > 0.0f);
    CHECK(water::buoyancyStep(target + 1.0f, target, 0.0f, 40.0f, 1.0f / 60.0f) < 0.0f);

    // Zero stiffness is a no-op, not a divide-by-zero.
    CHECK(water::buoyancyStep(target - 1.0f, target, 2.0f, 0.0f, 1.0f / 60.0f) == doctest::Approx(2.0f));
    CHECK(water::buoyancyStep(target - 1.0f, target, 2.0f, 40.0f, 0.0f) == doctest::Approx(2.0f));
}

TEST_CASE("buoyancy spring converges without overshooting") {
    const float target = 0.0f;
    const float stiffness = 36.0f;           // omega = 6 rad/s
    const float dt = 1.0f / 60.0f;

    float y = -2.0f;                         // dropped in, well below the float line
    float v = 0.0f;

    for (int i = 0; i < 600; ++i)            // 10 s
    {
        v = water::buoyancyStep(y, target, v, stiffness, dt);
        y += v * dt;

        // Critically damped: it must approach from below and never cross the surface.
        CHECK(y <= target + 1.0e-4f);
        CHECK(std::isfinite(y));
    }

    CHECK(y == doctest::Approx(target).epsilon(0.01));
    CHECK(std::abs(v) < 1.0e-2f);
}

TEST_CASE("buoyancy spring survives absurd stiffness and dt") {
    // Backward Euler is unconditionally stable, which is the reason for using it here: a hitch or an
    // over-tuned stiffness must not launch a character out of the map. Semi-implicit would explode.
    float y = -5.0f;
    float v = 0.0f;
    for (int i = 0; i < 200; ++i)
    {
        v = water::buoyancyStep(y, 0.0f, v, 1.0e6f, 1.0f);
        y += v * 1.0f;
        REQUIRE(std::isfinite(y));
        REQUIRE(std::isfinite(v));
    }
    CHECK(std::abs(y) < 5.0f);               // bounded, and heading toward the target
}

TEST_CASE("water drag decays velocity without ever reversing it") {
    CHECK(water::applyWaterDrag(4.0f, 0.0f, 0.5f) == doctest::Approx(4.0f));
    CHECK(water::applyWaterDrag(4.0f, 2.0f, 0.5f) == doctest::Approx(4.0f * std::exp(-1.0f)));
    CHECK(water::applyWaterDrag(0.0f, 5.0f, 0.5f) == doctest::Approx(0.0f));

    SUBCASE("a naive v -= v*drag*dt would flip the sign here") {
        // The invariant is that drag only ever shrinks the velocity toward zero: it never overshoots
        // into the opposite direction, which `v -= v*drag*dt` does as soon as drag*dt exceeds 1.
        // At extreme drag*dt the exponential underflows to exactly 0 - stopped, still not reversed.
        for (float drag : {1.0f, 10.0f, 100.0f, 1000.0f})
        {
            const float fwd = water::applyWaterDrag(3.0f, drag, 1.0f);
            const float back = water::applyWaterDrag(-3.0f, drag, 1.0f);

            CHECK(fwd >= 0.0f);
            CHECK(back <= 0.0f);
            CHECK(std::abs(fwd) <= 3.0f);
            CHECK(std::abs(back) <= 3.0f);
        }

        // At sane tunings it is a strict shrink, not a hard stop.
        CHECK(water::applyWaterDrag(3.0f, 3.0f, 1.0f / 60.0f) > 0.0f);
        CHECK(water::applyWaterDrag(3.0f, 3.0f, 1.0f / 60.0f) < 3.0f);
    }

    SUBCASE("negative drag is treated as none rather than as amplification") {
        CHECK(water::applyWaterDrag(4.0f, -10.0f, 1.0f) == doctest::Approx(4.0f));
    }
}

TEST_CASE("swim state has hysteresis around the threshold") {
    const float enter = 0.6f;

    // Rising through the threshold.
    CHECK_FALSE(water::swimStateFor(false, 0.55f, enter));
    CHECK(water::swimStateFor(false, 0.60f, enter));

    // Falling back: it takes a real drop, not a wobble, to stand up again.
    CHECK(water::swimStateFor(true, 0.55f, enter));
    CHECK_FALSE(water::swimStateFor(true, 0.45f, enter));

    SUBCASE("a character sitting exactly on the threshold does not flicker") {
        bool swimming = false;
        for (int i = 0; i < 50; ++i)
        {
            // Alternate a hair either side of the entry threshold, as standing in surf would.
            const float s = enter + ((i % 2 == 0) ? 0.005f : -0.005f);
            const bool next = water::swimStateFor(swimming, s, enter);
            if (i > 0)
                CHECK(next == swimming);      // latched after the first entry
            swimming = next;
        }
        CHECK(swimming);
    }
}

}   // TEST_SUITE
