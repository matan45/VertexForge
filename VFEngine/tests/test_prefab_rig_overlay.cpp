#include <doctest.h>

// VK-1433 Phase 1 — PrefabRig overlay geometry (pure, CPU-testable).
//
// PrefabRigOverlayGeometry builds the debug-line geometry for the rig visualization overlay
// (skeleton bones / socket axis triads / IK target markers). The functions are free + pure
// (no Vulkan, no assembly), so the line emission is verified directly here. The world-space
// joint/socket/IK formulas the controller feeds these are already locked by
// test_prefab_rig_assembly.cpp (socketModelTransform / ikTargetFromSocket).

#include "controllers/preview/PrefabRigOverlayGeometry.hpp"
#include "render/tools/ImmediateDebugTypes.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>

using namespace controllers::prefabrigoverlay;
using render::mesh::DebugLineVertex;
using render::mesh::ImmediateDebugDrawList;

namespace
{
    bool vecNear(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f)
    {
        return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
    }
}

TEST_SUITE("PrefabRigOverlayGeometry")
{

TEST_CASE("addLine appends exactly two vertices with the given endpoints and color")
{
    ImmediateDebugDrawList list;
    const glm::vec3 a{1.0f, 2.0f, 3.0f};
    const glm::vec3 b{4.0f, 5.0f, 6.0f};
    const glm::vec4 color{0.1f, 0.2f, 0.3f, 1.0f};

    addLine(list, a, b, color);

    REQUIRE(list.vertexCount() == 2);
    CHECK(vecNear(list.lineVertices[0].position, a));
    CHECK(vecNear(list.lineVertices[1].position, b));
    CHECK(list.lineVertices[0].color == color);
    CHECK(list.lineVertices[1].color == color);
}

TEST_CASE("addMarker emits a 3-axis cross centered on the position (6 vertices, 3 lines)")
{
    ImmediateDebugDrawList list;
    const glm::vec3 p{10.0f, 0.0f, -5.0f};
    const float h = 0.25f;
    const glm::vec4 color{1.0f, 1.0f, 0.0f, 1.0f};

    addMarker(list, p, h, color);

    REQUIRE(list.vertexCount() == 6);
    // X arm
    CHECK(vecNear(list.lineVertices[0].position, p - glm::vec3(h, 0, 0)));
    CHECK(vecNear(list.lineVertices[1].position, p + glm::vec3(h, 0, 0)));
    // Y arm
    CHECK(vecNear(list.lineVertices[2].position, p - glm::vec3(0, h, 0)));
    CHECK(vecNear(list.lineVertices[3].position, p + glm::vec3(0, h, 0)));
    // Z arm
    CHECK(vecNear(list.lineVertices[4].position, p - glm::vec3(0, 0, h)));
    CHECK(vecNear(list.lineVertices[5].position, p + glm::vec3(0, 0, h)));
    for (const DebugLineVertex& v : list.lineVertices)
        CHECK(v.color == color);
}

TEST_CASE("addAxisTriad places the triad at the transform origin, along its basis axes")
{
    ImmediateDebugDrawList list;
    const glm::vec3 origin{2.0f, 3.0f, 4.0f};
    const glm::mat4 xform = glm::translate(glm::mat4(1.0f), origin); // identity rotation
    const float len = 0.5f;

    addAxisTriad(list, xform, len);

    // 3 axes => 6 vertices.
    REQUIRE(list.vertexCount() == 6);
    // Each axis starts at the origin.
    CHECK(vecNear(list.lineVertices[0].position, origin));
    CHECK(vecNear(list.lineVertices[2].position, origin));
    CHECK(vecNear(list.lineVertices[4].position, origin));
    // X/Y/Z ends along world axes (identity rotation).
    CHECK(vecNear(list.lineVertices[1].position, origin + glm::vec3(len, 0, 0)));
    CHECK(vecNear(list.lineVertices[3].position, origin + glm::vec3(0, len, 0)));
    CHECK(vecNear(list.lineVertices[5].position, origin + glm::vec3(0, 0, len)));
    // X is reddish, Y greenish, Z blueish (sanity on the channel ordering).
    CHECK(list.lineVertices[1].color.r > list.lineVertices[1].color.g);
    CHECK(list.lineVertices[3].color.g > list.lineVertices[3].color.r);
    CHECK(list.lineVertices[5].color.b > list.lineVertices[5].color.r);
}

TEST_CASE("addAxisTriad rotates with the transform's basis")
{
    ImmediateDebugDrawList list;
    // 90 deg about Z maps local +X -> world +Y.
    const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));
    const float len = 1.0f;

    addAxisTriad(list, rot, len);

    REQUIRE(list.vertexCount() == 6);
    // X arm end should now point along +Y.
    CHECK(vecNear(list.lineVertices[1].position, glm::vec3(0, 1, 0)));
}

TEST_CASE("partColor cycles a fixed palette and is opaque")
{
    const glm::vec4 c0 = partColor(0);
    const glm::vec4 c1 = partColor(1);
    CHECK(c0 != c1);                  // adjacent parts differ
    CHECK(partColor(6) == c0);        // wraps at palette size (6)
    CHECK(partColor(0).a == doctest::Approx(1.0f));
}

// --- Gap coverage (VK-1433 Phase 1 tester) ---------------------------------

TEST_CASE("partColor: all six palette entries are pairwise distinct and opaque, then cycle")
{
    // Re-derive the palette directly from PrefabRigOverlayGeometry.cpp (the contract these
    // colors form: 6 distinct readable hues so adjacent parts never share a color).
    std::array<glm::vec4, 6> p;
    for (size_t i = 0; i < 6; ++i)
        p[i] = partColor(i);

    // Pairwise distinct across the whole palette (not just adjacent 0/1).
    for (size_t i = 0; i < 6; ++i)
        for (size_t j = i + 1; j < 6; ++j)
            CHECK_MESSAGE(p[i] != p[j], "palette entries " << i << " and " << j << " collide");

    // All opaque.
    for (size_t i = 0; i < 6; ++i)
        CHECK(p[i].a == doctest::Approx(1.0f));

    // Cycles with period 6 across more than one full wrap (palette[idx % 6]).
    for (size_t i = 0; i < 6; ++i)
    {
        CHECK(partColor(i + 6) == p[i]);
        CHECK(partColor(i + 12) == p[i]);
    }
}

TEST_CASE("partColor: deterministic — same index returns the same color across calls")
{
    for (size_t i = 0; i < 8; ++i)
        CHECK(partColor(i) == partColor(i));
}

TEST_CASE("addLine accumulates across calls (appends, does not overwrite)")
{
    ImmediateDebugDrawList list;
    const glm::vec4 c0{1.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec4 c1{0.0f, 1.0f, 0.0f, 1.0f};

    addLine(list, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), c0);
    addLine(list, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), c1);

    // Both segments survive, in order (4 vertices total).
    REQUIRE(list.vertexCount() == 4);
    CHECK(vecNear(list.lineVertices[0].position, glm::vec3(0.0f)));
    CHECK(vecNear(list.lineVertices[1].position, glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK(list.lineVertices[1].color == c0);
    CHECK(vecNear(list.lineVertices[2].position, glm::vec3(0.0f)));
    CHECK(vecNear(list.lineVertices[3].position, glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(list.lineVertices[3].color == c1);
}

TEST_CASE("addAxisTriad emits the documented X=red/Y=green/Z=blue colors exactly")
{
    ImmediateDebugDrawList list;
    addAxisTriad(list, glm::mat4(1.0f), 1.0f);

    REQUIRE(list.vertexCount() == 6);
    // Values pinned to PrefabRigOverlayGeometry.cpp (both endpoints of each axis share its color).
    const glm::vec4 xCol{1.0f, 0.15f, 0.15f, 1.0f};
    const glm::vec4 yCol{0.15f, 1.0f, 0.15f, 1.0f};
    const glm::vec4 zCol{0.15f, 0.35f, 1.0f, 1.0f};
    CHECK(list.lineVertices[0].color == xCol);
    CHECK(list.lineVertices[1].color == xCol);
    CHECK(list.lineVertices[2].color == yCol);
    CHECK(list.lineVertices[3].color == yCol);
    CHECK(list.lineVertices[4].color == zCol);
    CHECK(list.lineVertices[5].color == zCol);
}

TEST_CASE("addAxisTriad: all three axes start at the transform origin under rotation")
{
    ImmediateDebugDrawList list;
    const glm::vec3 origin{-3.0f, 7.0f, 2.0f};
    glm::mat4 xform = glm::rotate(glm::mat4(1.0f), glm::radians(37.0f), glm::normalize(glm::vec3(1, 2, 3)));
    xform[3] = glm::vec4(origin, 1.0f);

    addAxisTriad(list, xform, 0.75f);

    REQUIRE(list.vertexCount() == 6);
    // Indices 0/2/4 are the three axis start points — all at the origin regardless of rotation.
    CHECK(vecNear(list.lineVertices[0].position, origin));
    CHECK(vecNear(list.lineVertices[2].position, origin));
    CHECK(vecNear(list.lineVertices[4].position, origin));
}

TEST_CASE("addAxisTriad: axis segment length follows the basis-column scale times axisLength")
{
    // The cpp computes each arm as glm::vec3(transform[i]) * axisLength, so a non-unit basis
    // column (a scaled transform) scales the drawn segment by that column's magnitude. Pin it.
    ImmediateDebugDrawList list;
    const float axisLength = 0.5f;
    const glm::mat4 xform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));

    addAxisTriad(list, xform, axisLength);

    REQUIRE(list.vertexCount() == 6);
    // arm vector = (column basis * axisLength). Column 0 has length 2, col 1 -> 3, col 2 -> 4.
    const glm::vec3 xArm = list.lineVertices[1].position - list.lineVertices[0].position;
    const glm::vec3 yArm = list.lineVertices[3].position - list.lineVertices[2].position;
    const glm::vec3 zArm = list.lineVertices[5].position - list.lineVertices[4].position;
    CHECK(glm::length(xArm) == doctest::Approx(2.0f * axisLength));
    CHECK(glm::length(yArm) == doctest::Approx(3.0f * axisLength));
    CHECK(glm::length(zArm) == doctest::Approx(4.0f * axisLength));
}

TEST_CASE("addMarker: arm length scales with halfSize and degenerates to zero arms at halfSize 0")
{
    ImmediateDebugDrawList list;
    const glm::vec3 p{1.0f, -2.0f, 3.0f};

    addMarker(list, p, 0.0f, glm::vec4(1.0f));
    REQUIRE(list.vertexCount() == 6);
    // Every vertex collapses onto the point when halfSize == 0.
    for (const DebugLineVertex& v : list.lineVertices)
        CHECK(vecNear(v.position, p));

    // Non-zero halfSize: each arm spans exactly 2*halfSize along its axis.
    ImmediateDebugDrawList list2;
    const float h = 1.5f;
    addMarker(list2, p, h, glm::vec4(1.0f));
    REQUIRE(list2.vertexCount() == 6);
    CHECK(glm::length(list2.lineVertices[1].position - list2.lineVertices[0].position) == doctest::Approx(2.0f * h));
    CHECK(glm::length(list2.lineVertices[3].position - list2.lineVertices[2].position) == doctest::Approx(2.0f * h));
    CHECK(glm::length(list2.lineVertices[5].position - list2.lineVertices[4].position) == doctest::Approx(2.0f * h));
}

TEST_CASE("helpers append into a shared list (skeleton + socket + IK marker accumulate)")
{
    // The controller feeds one ImmediateDebugDrawList through all three helpers per frame;
    // verify the running count is the sum (append semantics across helper types).
    ImmediateDebugDrawList list;
    addLine(list, glm::vec3(0.0f), glm::vec3(0, 1, 0), partColor(0)); // a bone segment: +2
    addAxisTriad(list, glm::mat4(1.0f), 0.1f);                        // a socket frame: +6
    addMarker(list, glm::vec3(2.0f), 0.05f, partColor(1));           // an IK target: +6

    CHECK(list.vertexCount() == 2 + 6 + 6);
}

} // TEST_SUITE
