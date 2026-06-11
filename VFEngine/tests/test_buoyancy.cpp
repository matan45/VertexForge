#include <doctest.h>
#include <water/BuoyancySampling.hpp>
#include <glm/glm.hpp>

// ============================================================
// Multi-point buoyancy sampling (water epic Phase 1)
// ============================================================

TEST_SUITE("Buoyancy") {

// ---- generateSamplePoints ----

TEST_CASE("Box collider: center + 4 bottom corners") {
    glm::vec3 size{2.0f, 1.0f, 3.0f};
    auto samples = water::generateSamplePoints(types::ColliderShape::Box, size, 0.0f, glm::vec3(0.0f));

    REQUIRE(samples.count == 5);
    CHECK(samples.points[0] == glm::vec3(0.0f));

    // All corner points sit on the bottom face
    for (uint32_t i = 1; i < samples.count; ++i) {
        CHECK(samples.points[i].y == doctest::Approx(-size.y));
        CHECK(std::abs(samples.points[i].x) == doctest::Approx(size.x));
        CHECK(std::abs(samples.points[i].z) == doctest::Approx(size.z));
    }

    // Corners are symmetric: they sum to (0, -4*size.y, 0)
    glm::vec3 cornerSum(0.0f);
    for (uint32_t i = 1; i < samples.count; ++i)
        cornerSum += samples.points[i];
    CHECK(cornerSum.x == doctest::Approx(0.0f));
    CHECK(cornerSum.z == doctest::Approx(0.0f));
    CHECK(cornerSum.y == doctest::Approx(-4.0f * size.y));
}

TEST_CASE("Sphere collider: center + 4 equator points at radius") {
    float radius = 1.5f;
    auto samples = water::generateSamplePoints(types::ColliderShape::Sphere,
                                               glm::vec3(radius), 0.0f, glm::vec3(0.0f));

    REQUIRE(samples.count == 5);
    CHECK(samples.points[0] == glm::vec3(0.0f));
    for (uint32_t i = 1; i < samples.count; ++i) {
        CHECK(samples.points[i].y == doctest::Approx(0.0f));
        CHECK(glm::length(samples.points[i]) == doctest::Approx(radius));
    }
}

TEST_CASE("Capsule collider: center + both ends") {
    float radius = 0.5f;
    float height = 2.0f;
    auto samples = water::generateSamplePoints(types::ColliderShape::Capsule,
                                               glm::vec3(radius), height, glm::vec3(0.0f));

    REQUIRE(samples.count == 3);
    CHECK(samples.points[0] == glm::vec3(0.0f));
    CHECK(samples.points[1].y == doctest::Approx(-height * 0.5f));
    CHECK(samples.points[2].y == doctest::Approx(height * 0.5f));
}

TEST_CASE("Mesh collider falls back to single center point") {
    auto samples = water::generateSamplePoints(types::ColliderShape::TriangleMesh,
                                               glm::vec3(1.0f), 0.0f, glm::vec3(0.0f));
    REQUIRE(samples.count == 1);
    CHECK(samples.points[0] == glm::vec3(0.0f));
}

TEST_CASE("Collider offset shifts every sample point") {
    glm::vec3 offset{1.0f, 2.0f, 3.0f};
    auto withOffset = water::generateSamplePoints(types::ColliderShape::Box,
                                                  glm::vec3(1.0f), 0.0f, offset);
    auto noOffset = water::generateSamplePoints(types::ColliderShape::Box,
                                                glm::vec3(1.0f), 0.0f, glm::vec3(0.0f));

    REQUIRE(withOffset.count == noOffset.count);
    for (uint32_t i = 0; i < withOffset.count; ++i) {
        CHECK(withOffset.points[i].x == doctest::Approx(noOffset.points[i].x + offset.x));
        CHECK(withOffset.points[i].y == doctest::Approx(noOffset.points[i].y + offset.y));
        CHECK(withOffset.points[i].z == doctest::Approx(noOffset.points[i].z + offset.z));
    }
}

// ---- colliderHalfHeight ----

TEST_CASE("colliderHalfHeight matches legacy single-point heuristic") {
    CHECK(water::colliderHalfHeight(types::ColliderShape::Box, glm::vec3(2.0f, 1.5f, 1.0f), 0.0f)
          == doctest::Approx(1.5f));
    CHECK(water::colliderHalfHeight(types::ColliderShape::Sphere, glm::vec3(0.75f), 0.0f)
          == doctest::Approx(0.75f));
    CHECK(water::colliderHalfHeight(types::ColliderShape::Capsule, glm::vec3(0.5f), 2.0f)
          == doctest::Approx(1.5f));
    CHECK(water::colliderHalfHeight(types::ColliderShape::ConvexMesh, glm::vec3(9.0f), 9.0f)
          == doctest::Approx(0.5f));
}

// ---- computeSubmersion ----

TEST_CASE("Point at or above the surface is dry") {
    CHECK(water::computeSubmersion(5.0f, 5.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(water::computeSubmersion(6.0f, 5.0f, 1.0f) == doctest::Approx(0.0f));
}

TEST_CASE("Point one extent below the surface is fully submerged") {
    CHECK(water::computeSubmersion(4.0f, 5.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(water::computeSubmersion(0.0f, 5.0f, 1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("Submersion ramps linearly between surface and extent") {
    CHECK(water::computeSubmersion(4.5f, 5.0f, 1.0f) == doctest::Approx(0.5f));
    CHECK(water::computeSubmersion(4.75f, 5.0f, 1.0f) == doctest::Approx(0.25f));
}

TEST_CASE("Degenerate extent does not divide by zero") {
    CHECK(water::computeSubmersion(4.0f, 5.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(water::computeSubmersion(6.0f, 5.0f, 0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("Fully submerged box averages to 1 across all points") {
    glm::vec3 size{1.0f, 1.0f, 1.0f};
    auto samples = water::generateSamplePoints(types::ColliderShape::Box, size, 0.0f, glm::vec3(0.0f));
    float halfHeight = water::colliderHalfHeight(types::ColliderShape::Box, size, 0.0f);

    // Body center at y=0, water surface far above: every point fully submerged,
    // so the per-point force shares sum to exactly the legacy single-point force.
    float total = 0.0f;
    for (uint32_t i = 0; i < samples.count; ++i)
        total += water::computeSubmersion(samples.points[i].y, 100.0f, halfHeight);
    CHECK(total / static_cast<float>(samples.count) == doctest::Approx(1.0f));
}

TEST_CASE("Tilted hull: lower points engage before higher points") {
    // Two points of a hull, one dipped lower than the other
    float waterHeight = 0.0f;
    float extent = 1.0f;
    float lowSide = water::computeSubmersion(-0.8f, waterHeight, extent);
    float highSide = water::computeSubmersion(-0.2f, waterHeight, extent);
    CHECK(lowSide > highSide);
    CHECK(lowSide == doctest::Approx(0.8f));
    CHECK(highSide == doctest::Approx(0.2f));
}

}
