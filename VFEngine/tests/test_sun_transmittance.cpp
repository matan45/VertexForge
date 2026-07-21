// VK-1566 — CPU sun-transmittance evaluation + day-night sun-entity rotation convention.
// Golden values were produced by evaluating the same function for the default
// AtmosphereSettings; they double as a regression snapshot and a physical-plausibility check.
#include "doctest.h"

#include <glm/glm.hpp>

#include <atmosphere/SunTransmittance.hpp>
#include <atmosphere/SunEntitySync.hpp>
#include <components/CoreComponents.hpp>

using render::atmosphere::AtmosphereSettings;
using render::atmosphere::evaluateSunTransmittance;
using render::atmosphere::directionalLightEulerForSun;

namespace
{
    // dirToSun for azimuth 0 at a given elevation (matches AtmosphereTypes.hpp
    // sunDirectionFromAngles with az=0: (0, sin el, cos el)).
    glm::vec3 dirFromElevation(float elevationDeg)
    {
        const float el = glm::radians(elevationDeg);
        return glm::vec3(0.0f, std::sin(el), std::cos(el));
    }

    // Full azimuth/elevation direction (mirror of sunDirectionFromAngles, kept local so the
    // test has no Graphics-module dependency).
    glm::vec3 dirFromAngles(float azDeg, float elDeg)
    {
        const float az = glm::radians(azDeg);
        const float el = glm::radians(elDeg);
        const float cosEl = std::cos(el);
        return glm::vec3(cosEl * std::sin(az), std::sin(el), cosEl * std::cos(az));
    }
}

TEST_CASE("sun transmittance: below the horizon yields zero")
{
    AtmosphereSettings s; // defaults

    CHECK(evaluateSunTransmittance(s, dirFromElevation(-10.0f), 0.0f) == glm::vec3(0.0f));
    CHECK(evaluateSunTransmittance(s, dirFromElevation(-0.01f), 0.0f) == glm::vec3(0.0f));
    // Exactly on the horizon (cosZenith == 0) also returns zero.
    CHECK(evaluateSunTransmittance(s, dirFromElevation(0.0f), 0.0f) == glm::vec3(0.0f));
    // A zero-length direction is degenerate -> zero (no divide-by-zero).
    CHECK(evaluateSunTransmittance(s, glm::vec3(0.0f), 0.0f) == glm::vec3(0.0f));
}

TEST_CASE("sun transmittance: zenith is bright and near-white, red-biased")
{
    AtmosphereSettings s;
    const glm::vec3 t = evaluateSunTransmittance(s, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);

    // Golden snapshot (default settings, ground level).
    CHECK(t.x == doctest::Approx(0.937595f).epsilon(0.005));
    CHECK(t.y == doctest::Approx(0.865283f).epsilon(0.005));
    CHECK(t.z == doctest::Approx(0.760743f).epsilon(0.005));

    // Straight up: little atmosphere -> all channels bright, but blue scatters most.
    CHECK(t.x > 0.7f);
    CHECK(t.z > 0.5f);
    CHECK(t.x > t.y);
    CHECK(t.y > t.z);
}

TEST_CASE("sun transmittance: horizon is strongly red-shifted, blue extinguished")
{
    AtmosphereSettings s;

    const glm::vec3 low = evaluateSunTransmittance(s, dirFromElevation(2.0f), 0.0f);
    CHECK(low.x == doctest::Approx(0.294882f).epsilon(0.005));
    CHECK(low.y == doctest::Approx(0.076055f).epsilon(0.005));
    CHECK(low.z == doctest::Approx(0.005504f).epsilon(0.01));

    // Red survives, green partly, blue almost fully scattered out.
    CHECK(low.x > low.y);
    CHECK(low.y > low.z);
    CHECK(low.z < 0.05f);
    CHECK(low.x > 0.2f);
}

TEST_CASE("sun transmittance: monotonic increasing per channel with elevation")
{
    AtmosphereSettings s;
    const float elevations[] = {1.0f, 5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 60.0f, 90.0f};

    glm::vec3 prev = evaluateSunTransmittance(s, dirFromElevation(elevations[0]), 0.0f);
    for (int i = 1; i < 8; ++i)
    {
        const glm::vec3 cur = evaluateSunTransmittance(s, dirFromElevation(elevations[i]), 0.0f);
        // Higher sun = shorter path = more light survives, every channel.
        CHECK(cur.x > prev.x);
        CHECK(cur.y > prev.y);
        CHECK(cur.z > prev.z);
        // Ordering holds at every elevation.
        CHECK(cur.x >= cur.y);
        CHECK(cur.y >= cur.z);
        prev = cur;
    }
}

TEST_CASE("sun transmittance: higher altitude means whiter sun")
{
    AtmosphereSettings s;
    const glm::vec3 ground = evaluateSunTransmittance(s, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
    const glm::vec3 high = evaluateSunTransmittance(s, glm::vec3(0.0f, 1.0f, 0.0f), 10000.0f);

    // Less atmosphere overhead -> higher transmittance on every channel and less color bias.
    CHECK(high.x > ground.x);
    CHECK(high.y > ground.y);
    CHECK(high.z > ground.z);
    CHECK((high.x - high.z) < (ground.x - ground.z));
    CHECK(high.z == doctest::Approx(0.925988f).epsilon(0.005));
}

TEST_CASE("sun entity euler: -Z matches the atmosphere sun direction across the sphere")
{
    // The day-night sync rotates the first directional light so its local -Z (light-travel
    // direction) equals -sunDirectionFromAngles. Verify against the real
    // TransformComponent::getMatrix used in production (NOT the naive {-el, az, 0} triple,
    // which only holds near azimuth 0).
    const float azs[] = {0.0f, 33.0f, 90.0f, 135.0f, 180.0f, 225.0f, 270.0f, 315.0f};
    const float els[] = {5.0f, 30.0f, 45.0f, 60.0f, 85.0f};

    for (float az : azs)
    {
        for (float el : els)
        {
            components::TransformComponent tc;
            tc.rotation = directionalLightEulerForSun(az, el);

            const glm::vec3 travel = glm::vec3(tc.getMatrix() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
            const glm::vec3 expected = -dirFromAngles(az, el); // entity -Z points away from the sun

            CHECK(travel.x == doctest::Approx(expected.x).epsilon(0.001));
            CHECK(travel.y == doctest::Approx(expected.y).epsilon(0.001));
            CHECK(travel.z == doctest::Approx(expected.z).epsilon(0.001));
        }
    }
}
