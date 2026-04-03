#include <doctest.h>
#include <destruction/DestructionTypes.hpp>
#include <destruction/MeshClipper.hpp>
#include "test_destruction_helpers.hpp"
#include <glm/gtc/constants.hpp>

// ============================================================
// MeshClipper tests
// ============================================================

TEST_SUITE("MeshClipper") {

TEST_CASE("clipToHalfSpace: mesh entirely on positive side returns unchanged") {
    auto cube = makeUnitCube();
    const auto& lod = cube.lodLevels[0];

    // Plane at x = -5 (everything is positive)
    glm::vec4 plane(1.0f, 0.0f, 0.0f, 5.0f);
    auto result = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, plane);

    CHECK_FALSE(result.didClip);
    CHECK(result.vertices.size() == lod.vertices.size());
    CHECK(result.indices.size() == lod.indices.size());
    CHECK(result.cutEdges.empty());
}

TEST_CASE("clipToHalfSpace: mesh entirely on negative side returns empty") {
    auto cube = makeUnitCube();
    const auto& lod = cube.lodLevels[0];

    // Plane at x = 5 pointing +x (everything is negative)
    glm::vec4 plane(1.0f, 0.0f, 0.0f, -5.0f);
    auto result = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, plane);

    CHECK(result.didClip);
    CHECK(result.vertices.empty());
    CHECK(result.indices.empty());
}

TEST_CASE("clipToHalfSpace: clip cube in half produces geometry on positive side") {
    auto cube = makeUnitCube();
    const auto& lod = cube.lodLevels[0];

    // Plane at x = 0, keeping +x side
    glm::vec4 plane(1.0f, 0.0f, 0.0f, 0.0f);
    auto result = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, plane);

    CHECK(result.didClip);
    CHECK(result.vertices.size() > 0);
    CHECK(result.indices.size() > 0);
    CHECK(result.indices.size() % 3 == 0);
    CHECK(result.cutEdges.size() > 0);

    // All resulting vertices should have x >= -epsilon
    for (const auto& v : result.vertices)
    {
        CHECK(v.position.x >= -1e-5f);
    }
}

TEST_CASE("clipToHalfSpace: clip preserves vertex attributes") {
    auto cube = makeUnitCube();
    const auto& lod = cube.lodLevels[0];

    // Clip at x = 0
    glm::vec4 plane(1.0f, 0.0f, 0.0f, 0.0f);
    auto result = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, plane);

    for (const auto& v : result.vertices)
    {
        // Normals should be unit length
        float len = glm::length(v.normal);
        CHECK(len == doctest::Approx(1.0f).epsilon(0.01f));

        // Bone indices should be -1 (no skinning)
        CHECK(v.boneIndices.x == -1);
    }
}

TEST_CASE("clipToHalfSpace: empty input returns empty result") {
    std::vector<resource::Vertex> emptyVerts;
    std::vector<uint32_t> emptyIndices;

    glm::vec4 plane(1.0f, 0.0f, 0.0f, 0.0f);
    auto result = destruction::MeshClipper::clipToHalfSpace(emptyVerts, emptyIndices, plane);

    CHECK(result.vertices.empty());
    CHECK(result.indices.empty());
    CHECK_FALSE(result.didClip);
}

TEST_CASE("clipToHalfSpace: clip at different positions produces different amounts of geometry") {
    auto cube = makeUnitCube();
    const auto& lod = cube.lodLevels[0];

    // Clip keeping most of the cube (x > -0.4)
    glm::vec4 planeMost(1.0f, 0.0f, 0.0f, 0.4f);
    auto resultMost = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, planeMost);

    // Clip keeping less of the cube (x > 0.4)
    glm::vec4 planeLess(1.0f, 0.0f, 0.0f, -0.4f);
    auto resultLess = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, planeLess);

    // The "most" clip should keep more triangles than the "less" clip
    CHECK(resultMost.indices.size() >= resultLess.indices.size());
}

TEST_CASE("clipToHalfSpace: clipping tetrahedron produces valid geometry") {
    auto tet = makeTetrahedron();
    const auto& lod = tet.lodLevels[0];

    // Horizontal clip at y = 0.5
    glm::vec4 plane(0.0f, 1.0f, 0.0f, -0.5f);
    auto result = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, plane);

    CHECK(result.didClip);
    CHECK(result.vertices.size() > 0);
    CHECK(result.indices.size() % 3 == 0);

    // All resulting vertices should have y >= 0.5 - epsilon
    for (const auto& v : result.vertices)
    {
        CHECK(v.position.y >= 0.5f - 1e-4f);
    }
}

} // TEST_SUITE MeshClipper
