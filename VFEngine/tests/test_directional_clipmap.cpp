#include <doctest.h>
#include <render/shadow/DirectionalShadowCalculator.hpp>

#include <glm/glm.hpp>
#include <cmath>

// ============================================================
// DirectionalShadowCalculator: clipmap level math, texel snapping,
// and orthographic matrix construction (CPU-only, header-only).
//
// These guard the two riskiest correctness properties of the
// directional Virtual Shadow Map clipmap:
//   1. stable texel-snapping (sub-texel camera motion must NOT shift
//      the shadow grid — the classic shimmer fix), and
//   2. monotonic, doubling level extents + correct level selection.
// ============================================================

using render::shadow::DirectionalShadowCalculator;
using render::shadow::ClipmapLevel;

namespace
{
    bool matApproxEqual(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (std::abs(a[i][j] - b[i][j]) > eps)
                    return false;
        return true;
    }
}

TEST_CASE("clipmap level extents double per level")
{
    CHECK(DirectionalShadowCalculator::levelExtent(32.0f, 0) == doctest::Approx(32.0f));
    CHECK(DirectionalShadowCalculator::levelExtent(32.0f, 1) == doctest::Approx(64.0f));
    CHECK(DirectionalShadowCalculator::levelExtent(32.0f, 2) == doctest::Approx(128.0f));
    CHECK(DirectionalShadowCalculator::levelExtent(32.0f, 5) == doctest::Approx(1024.0f));
}

TEST_CASE("clipmap texel size")
{
    // full extent = 2 * halfExtent; texels = pagesPerLevel * pageSize
    // level 0: 64 world units / (8 * 128 = 1024 texels) = 0.0625 wu/texel
    float t = DirectionalShadowCalculator::levelTexelSize(32.0f, 0, 8, 128);
    CHECK(t == doctest::Approx(0.0625f));
    // level 1 covers 2x the area at the same texel count -> 2x the world texel
    float t1 = DirectionalShadowCalculator::levelTexelSize(32.0f, 1, 8, 128);
    CHECK(t1 == doctest::Approx(0.125f));
}

TEST_CASE("level selection grows monotonically with distance and clamps")
{
    const float base = 32.0f;
    const uint32_t levels = 6;

    CHECK(DirectionalShadowCalculator::selectLevel(10.0f, base, levels) == 0);   // inside level 0
    CHECK(DirectionalShadowCalculator::selectLevel(50.0f, base, levels) == 1);   // 1.56x -> ceil(log2)=1
    CHECK(DirectionalShadowCalculator::selectLevel(100.0f, base, levels) == 2);  // 3.13x -> ceil(log2)=2
    CHECK(DirectionalShadowCalculator::selectLevel(500.0f, base, levels) == 4);  // 15.6x -> ceil(log2)=4
    CHECK(DirectionalShadowCalculator::selectLevel(50000.0f, base, levels) == levels - 1); // clamped

    // Monotonic non-decreasing with distance.
    uint32_t prev = 0;
    for (float d = 1.0f; d < 4000.0f; d *= 1.3f)
    {
        uint32_t lvl = DirectionalShadowCalculator::selectLevel(d, base, levels);
        CHECK(lvl >= prev);
        prev = lvl;
    }
}

TEST_CASE("texel snapping is stable and idempotent")
{
    const float texel = 0.0625f;

    // Idempotent: snapping an already-snapped value is a no-op.
    float snapped = DirectionalShadowCalculator::snapToTexel(100.1234f, texel);
    CHECK(DirectionalShadowCalculator::snapToTexel(snapped, texel) == doctest::Approx(snapped));

    // Sub-texel motion within the same cell snaps to the SAME value (anti-shimmer).
    float a = DirectionalShadowCalculator::snapToTexel(100.01f, texel);
    float b = DirectionalShadowCalculator::snapToTexel(100.05f, texel);
    CHECK(a == doctest::Approx(b));

    // Crossing a texel boundary moves the snapped value by exactly one texel.
    float c = DirectionalShadowCalculator::snapToTexel(100.10f, texel);
    CHECK(c == doctest::Approx(a - texel)); // floor goes to the next lower grid line
}

TEST_CASE("computeClipmapLevels produces concentric, camera-centered shells")
{
    glm::vec3 dir = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));
    glm::vec3 camPos(100.0f, 5.0f, -50.0f);

    auto levels = DirectionalShadowCalculator::computeClipmapLevels(
        dir, camPos, 32.0f, 6, 8, 128, 4000.0f);

    REQUIRE(levels.size() == 6);

    for (uint32_t i = 0; i < levels.size(); ++i)
    {
        // Extents double per level.
        CHECK(levels[i].extent == doctest::Approx(32.0f * static_cast<float>(1u << i)));

        // The camera position projects near the center of every level (within the texel
        // snap offset, never near the edge).
        glm::vec4 clip = levels[i].viewProjMatrix * glm::vec4(camPos, 1.0f);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        CHECK(std::abs(ndc.x) < 0.5f);
        CHECK(std::abs(ndc.y) < 0.5f);
        CHECK(ndc.z >= 0.0f);
        CHECK(ndc.z <= 1.0f);
    }
}

TEST_CASE("clipmap level 0 does not shift under sub-texel camera motion")
{
    // Light along -Z makes the light-space "right" axis world X, so moving the camera in X
    // moves along the snap axis. level-0 texel = 0.0625 world units.
    glm::vec3 dir(0.0f, 0.0f, -1.0f);

    auto a = DirectionalShadowCalculator::computeClipmapLevels(
        dir, glm::vec3(100.01f, 5.0f, -50.0f), 32.0f, 6, 8, 128, 4000.0f);
    auto b = DirectionalShadowCalculator::computeClipmapLevels(
        dir, glm::vec3(100.05f, 5.0f, -50.0f), 32.0f, 6, 8, 128, 4000.0f);
    auto c = DirectionalShadowCalculator::computeClipmapLevels(
        dir, glm::vec3(100.10f, 5.0f, -50.0f), 32.0f, 6, 8, 128, 4000.0f);

    // Sub-texel move (0.04 < 0.0625): level 0 matrix is bit-stable -> no shimmer.
    CHECK(matApproxEqual(a[0].viewProjMatrix, b[0].viewProjMatrix));

    // Crossing a texel boundary: level 0 re-centers (matrix changes).
    CHECK_FALSE(matApproxEqual(a[0].viewProjMatrix, c[0].viewProjMatrix));
}
