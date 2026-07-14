#include <doctest.h>
#include <math/TransformUtils.hpp>
#include <glm/glm.hpp>
#include <cmath>

// ============================================================
// VK-1505: shared spatial-audio helpers extracted into math/TransformUtils.hpp.
//
//   * forwardFromEulerDegrees — the engine's -Z-forward vector from an Euler
//     rotation (x = pitch, y = yaw) in degrees. Single source of truth for
//     AudioAPI (play-time source direction) and AudioSceneUpdater (per-frame
//     emitter follow + listener forward).
//   * positionMovedBeyond — squared-distance dirty-check that gates per-frame
//     re-dispatch of a moving 3D source's transform.
//
// CPU-only: no OpenAL device, no EnTT registry — the helpers are pure.
// ============================================================

namespace
{
    constexpr float kEps = 1e-4f;

    bool approxVec(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
    {
        return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps &&
               std::abs(a.z - b.z) < eps;
    }

    // The inline formula that lived — byte-for-byte identical — in AudioAPI.cpp and
    // AudioSceneUpdater.cpp before the extraction. Retained here so the refactor can be
    // proven behaviour-preserving.
    glm::vec3 legacyForward(const glm::vec3& rotationDeg)
    {
        const float yawRad = glm::radians(rotationDeg.y);
        const float pitchRad = glm::radians(rotationDeg.x);
        glm::vec3 forward;
        forward.x = -std::sin(yawRad) * std::cos(pitchRad);
        forward.y = std::sin(pitchRad);
        forward.z = -std::cos(yawRad) * std::cos(pitchRad);
        return glm::normalize(forward);
    }
}

TEST_SUITE("AudioSpatial")
{
    TEST_CASE("forwardFromEulerDegrees: cardinal directions")
    {
        CHECK(approxVec(math::forwardFromEulerDegrees(glm::vec3(0.0f, 0.0f, 0.0f)),
                        glm::vec3(0.0f, 0.0f, -1.0f)));
        CHECK(approxVec(math::forwardFromEulerDegrees(glm::vec3(0.0f, 90.0f, 0.0f)),
                        glm::vec3(-1.0f, 0.0f, 0.0f)));
        CHECK(approxVec(math::forwardFromEulerDegrees(glm::vec3(0.0f, 180.0f, 0.0f)),
                        glm::vec3(0.0f, 0.0f, 1.0f)));
        CHECK(approxVec(math::forwardFromEulerDegrees(glm::vec3(0.0f, -90.0f, 0.0f)),
                        glm::vec3(1.0f, 0.0f, 0.0f)));
        CHECK(approxVec(math::forwardFromEulerDegrees(glm::vec3(90.0f, 0.0f, 0.0f)),
                        glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    TEST_CASE("forwardFromEulerDegrees: result is unit length")
    {
        const glm::vec3 f = math::forwardFromEulerDegrees(glm::vec3(37.0f, 200.0f, 0.0f));
        CHECK(std::abs(glm::length(f) - 1.0f) < 1e-4f);
    }

    TEST_CASE("forwardFromEulerDegrees: matches the original inline formula (refactor guard)")
    {
        const glm::vec3 samples[] = {
            {0.0f, 0.0f, 0.0f},   {12.0f, 34.0f, 0.0f},   {-45.0f, 120.0f, 0.0f},
            {80.0f, -175.0f, 0.0f}, {37.0f, 200.0f, 0.0f}, {-60.0f, -300.0f, 0.0f},
        };
        for (const auto& r : samples)
        {
            CHECK(approxVec(math::forwardFromEulerDegrees(r), legacyForward(r)));
        }
    }

    TEST_CASE("positionMovedBeyond: epsilon dirty-check")
    {
        constexpr float eps = 1e-3f;
        const glm::vec3 a(1.0f, 2.0f, 3.0f);

        // Identical / below-epsilon delta -> not moved.
        CHECK_FALSE(math::positionMovedBeyond(a, a, eps));
        CHECK_FALSE(math::positionMovedBeyond(a, a + glm::vec3(5e-4f, 0.0f, 0.0f), eps));

        // Above-epsilon on a single axis -> moved.
        CHECK(math::positionMovedBeyond(a, a + glm::vec3(2e-3f, 0.0f, 0.0f), eps));

        // Euclidean, not per-axis: each component < eps but |d| ~= 1.39e-3 > eps.
        CHECK(math::positionMovedBeyond(a, a + glm::vec3(8e-4f, 8e-4f, 8e-4f), eps));

        // Symmetric in its arguments.
        const glm::vec3 b = a + glm::vec3(2e-3f, 0.0f, 0.0f);
        CHECK(math::positionMovedBeyond(a, b, eps) == math::positionMovedBeyond(b, a, eps));
    }
}
