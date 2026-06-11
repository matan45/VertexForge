#include <doctest.h>
#include <water/FoamMath.hpp>
#include <cmath>

// ============================================================
// Persistent foam math (water epic Phase 3) — CPU mirror of ocean_merge.glsl
// ============================================================

TEST_SUITE("FoamMath") {

// ---- decay ----

TEST_CASE("Decay half-life matches exp(-decay * t)") {
    // decay = ln(2) gives a 1-second half-life
    float halfLifeDecay = std::log(2.0f);
    CHECK(water::foamDecayStep(1.0f, halfLifeDecay, 1.0f) == doctest::Approx(0.5f));
    CHECK(water::foamDecayStep(1.0f, halfLifeDecay, 2.0f) == doctest::Approx(0.25f));
}

TEST_CASE("Zero dt leaves foam untouched (paused frame)") {
    CHECK(water::foamDecayStep(0.7f, 0.5f, 0.0f) == doctest::Approx(0.7f));
}

TEST_CASE("Decay is multiplicative and frame-rate independent") {
    // Two 0.5 s steps equal one 1.0 s step
    float twoSteps = water::foamDecayStep(water::foamDecayStep(1.0f, 0.8f, 0.5f), 0.8f, 0.5f);
    float oneStep = water::foamDecayStep(1.0f, 0.8f, 1.0f);
    CHECK(twoSteps == doctest::Approx(oneStep));
}

// ---- advection ----

TEST_CASE("Advection samples upstream of the chop displacement") {
    glm::vec2 uv{0.5f, 0.5f};
    glm::vec2 chop{10.0f, 0.0f};  // 10 m of +X displacement on a 100 m patch
    auto prevUV = water::advectedFoamUV(uv, chop, 100.0f, 1.0f);
    CHECK(prevUV.x == doctest::Approx(0.4f));
    CHECK(prevUV.y == doctest::Approx(0.5f));
}

TEST_CASE("Zero dt or zero displacement means no advection") {
    glm::vec2 uv{0.25f, 0.75f};
    auto noTime = water::advectedFoamUV(uv, glm::vec2(5.0f, -3.0f), 100.0f, 0.0f);
    CHECK(noTime.x == doctest::Approx(uv.x));
    CHECK(noTime.y == doctest::Approx(uv.y));

    auto noChop = water::advectedFoamUV(uv, glm::vec2(0.0f), 100.0f, 0.016f);
    CHECK(noChop.x == doctest::Approx(uv.x));
    CHECK(noChop.y == doctest::Approx(uv.y));
}

TEST_CASE("UV wraps across the periodic patch border") {
    auto wrappedNeg = water::wrapUV(glm::vec2(-0.1f, 0.5f));
    CHECK(wrappedNeg.x == doctest::Approx(0.9f));
    CHECK(wrappedNeg.y == doctest::Approx(0.5f));

    auto wrappedOver = water::wrapUV(glm::vec2(1.25f, 2.5f));
    CHECK(wrappedOver.x == doctest::Approx(0.25f));
    CHECK(wrappedOver.y == doctest::Approx(0.5f));
}

// ---- persistence blend ----

TEST_CASE("Persistence 0 reduces to instantaneous foam") {
    CHECK(water::combineFoam(0.3f, 0.9f, 0.0f) == doctest::Approx(0.3f));
    CHECK(water::combineFoam(0.0f, 0.9f, 0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("History survives after the crest stops folding") {
    // No new foam this frame, but history persists scaled by persistence
    CHECK(water::combineFoam(0.0f, 0.8f, 0.85f) == doctest::Approx(0.8f * 0.85f));
}

TEST_CASE("Fresh foam is never dimmer than the instantaneous value") {
    // Weak history must not drag a strong fresh crest down
    CHECK(water::combineFoam(0.9f, 0.1f, 0.85f) >= 0.9f);
}

TEST_CASE("Foam dies out completely under decay + blend") {
    // Simulate 10 seconds at 60 fps with no new foam: trail should be ~gone
    float foam = 1.0f;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 600; ++i) {
        float surviving = water::foamDecayStep(foam, 0.5f, dt);
        foam = water::combineFoam(0.0f, surviving, 0.85f);
    }
    CHECK(foam < 0.01f);
}

}
