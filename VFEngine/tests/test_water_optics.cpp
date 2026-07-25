#include <doctest.h>
#include <water/WaterOptics.hpp>

// ============================================================
// VK-1604: Beer-Lambert water optics (CPU twin of water.glsl)
// ============================================================

TEST_SUITE("WaterOptics") {

TEST_CASE("transmittance: zero path length is fully transparent") {
    const glm::vec3 absorption(0.45f, 0.08f, 0.02f);

    // This identity is what underwrites "feature off == unchanged": at zero depth the water
    // must not tint the refracted scene at all.
    const glm::vec3 t = water::transmittance(absorption, 0.0f);
    CHECK(t.r == doctest::Approx(1.0f));
    CHECK(t.g == doctest::Approx(1.0f));
    CHECK(t.b == doctest::Approx(1.0f));

    // Negative path lengths are clamped, not extrapolated into amplification.
    const glm::vec3 tNeg = water::transmittance(absorption, -5.0f);
    CHECK(tNeg.r == doctest::Approx(1.0f));
    CHECK(tNeg.b == doctest::Approx(1.0f));
}

TEST_CASE("transmittance: decays monotonically and stays in [0,1]") {
    const glm::vec3 absorption(0.45f, 0.08f, 0.02f);

    float previous = 2.0f;
    for (float depth = 0.0f; depth <= 40.0f; depth += 2.0f)
    {
        const glm::vec3 t = water::transmittance(absorption, depth);
        CHECK(t.r <= previous);
        CHECK(t.r >= 0.0f);
        CHECK(t.r <= 1.0f);
        previous = t.r;
    }
}

TEST_CASE("transmittance: red extinguishes first with clear-water defaults") {
    // The whole reason deep water reads blue: red is absorbed ~20x faster than blue.
    const glm::vec3 absorption(0.45f, 0.08f, 0.02f);
    const glm::vec3 t = water::transmittance(absorption, 10.0f);

    CHECK(t.r < t.g);
    CHECK(t.g < t.b);
}

TEST_CASE("transmittance: channels are independent") {
    // A channel with zero absorption must pass through untouched no matter how deep.
    const glm::vec3 absorption(0.5f, 0.0f, 0.25f);
    const glm::vec3 t = water::transmittance(absorption, 30.0f);

    CHECK(t.g == doctest::Approx(1.0f));
    CHECK(t.r < 1.0f);
    CHECK(t.b < 1.0f);
    CHECK(t.r < t.b);
}

TEST_CASE("inScatter: starts at zero and saturates to the scattering colour") {
    const glm::vec3 scatterColor(0.0f, 0.35f, 0.30f);
    const glm::vec3 scatterCoeff(0.05f, 0.05f, 0.04f);

    const glm::vec3 atZero = water::inScatter(scatterColor, scatterCoeff, 0.0f);
    CHECK(atZero.g == doctest::Approx(0.0f));
    CHECK(atZero.b == doctest::Approx(0.0f));

    const glm::vec3 farAway = water::inScatter(scatterColor, scatterCoeff, 5000.0f);
    CHECK(farAway.g == doctest::Approx(scatterColor.g));
    CHECK(farAway.b == doctest::Approx(scatterColor.b));

    // Monotone increase toward the asymptote.
    CHECK(water::inScatter(scatterColor, scatterCoeff, 5.0f).g
          < water::inScatter(scatterColor, scatterCoeff, 20.0f).g);
}

TEST_CASE("refractedPathLength: longer than the vertical depth, and clamped") {
    // Head-on (NdotV = 1) the path is depth down + depth up, averaged -> depth.
    CHECK(water::refractedPathLength(10.0f, 1.0f, 1000.0f) == doctest::Approx(10.0f));

    // At a grazing angle the return trip is much longer.
    const float grazing = water::refractedPathLength(10.0f, 0.2f, 1000.0f);
    CHECK(grazing > 10.0f);

    // Never shorter than the vertical depth, for any view angle.
    for (float ndotv = 0.05f; ndotv <= 1.0f; ndotv += 0.05f)
        CHECK(water::refractedPathLength(7.0f, ndotv, 1000.0f) >= doctest::Approx(7.0f));

    // The clamp is what keeps deep water from going fully black.
    CHECK(water::refractedPathLength(10000.0f, 1.0f, 30.0f) == doctest::Approx(30.0f));
    CHECK(water::refractedPathLength(-5.0f, 1.0f, 30.0f) == doctest::Approx(0.0f));
}

TEST_CASE("applyWaterOptics: shallow water barely alters the refracted colour") {
    const glm::vec3 scene(0.6f, 0.5f, 0.4f);
    const glm::vec3 absorption(0.45f, 0.08f, 0.02f);
    const glm::vec3 scatterColor(0.0f, 0.35f, 0.30f);
    const glm::vec3 scatterCoeff(0.05f, 0.05f, 0.04f);

    const glm::vec3 atZero = water::applyWaterOptics(scene, absorption, scatterColor,
                                                      scatterCoeff, 0.0f);
    CHECK(atZero.r == doctest::Approx(scene.r));
    CHECK(atZero.g == doctest::Approx(scene.g));
    CHECK(atZero.b == doctest::Approx(scene.b));

    // Deep water: red is gone, and the result trends toward the scattering colour.
    const glm::vec3 deep = water::applyWaterOptics(scene, absorption, scatterColor,
                                                    scatterCoeff, 60.0f);
    CHECK(deep.r < 0.01f);
    CHECK(deep.b > deep.r);
}

TEST_CASE("linearizeDepth: matches the shared cluster_culling.glsl helper") {
    const float nearPlane = 0.1f;
    const float farPlane = 1000.0f;

    // A standard (non-reverse) projection maps the near plane to windowZ 0 and far to 1.
    CHECK(water::linearizeDepth(nearPlane, farPlane, 0.0f) == doctest::Approx(nearPlane));
    CHECK(water::linearizeDepth(nearPlane, farPlane, 1.0f) == doctest::Approx(farPlane));

    // Monotone increasing in windowZ — required for the SSR thickness test to mean anything.
    float previous = -1.0f;
    for (float z = 0.0f; z <= 1.0f; z += 0.05f)
    {
        const float linear = water::linearizeDepth(nearPlane, farPlane, z);
        CHECK(linear > previous);
        previous = linear;
    }
}

TEST_CASE("linearizeDepth: round-trips against the projection it inverts") {
    const float nearPlane = 0.1f;
    const float farPlane = 500.0f;

    // Forward: view-space z -> [0,1] window depth for a standard perspective projection.
    auto windowZFromView = [&](float viewZ) {
        return (farPlane / (farPlane - nearPlane)) * (1.0f - nearPlane / viewZ);
    };

    // Kept well away from the far plane on purpose: there windowZ approaches 1.0 and the
    // (far - z*(far-near)) term cancels catastrophically in float32, so a tight tolerance would
    // be measuring float precision rather than the inverse relationship under test.
    for (float viewZ : {0.5f, 1.0f, 10.0f, 100.0f})
    {
        const float roundTripped = water::linearizeDepth(nearPlane, farPlane, windowZFromView(viewZ));
        CHECK(roundTripped == doctest::Approx(viewZ).epsilon(0.002));
    }
}

} // TEST_SUITE("WaterOptics")
