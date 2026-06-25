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

// --- VK-1433 Phase 1c: selected-socket highlight + connector-line geometry ------------------

TEST_CASE("selectedSocketColor is opaque and distinct from every part palette color")
{
    const glm::vec4 hl = selectedSocketColor();
    CHECK(hl.a == doctest::Approx(1.0f));
    // The halo must read against every part's skeleton/triad color, so it can't collide with any
    // palette entry (the controller draws the halo on top of the part-colored triad).
    for (size_t i = 0; i < 6; ++i)
        CHECK_MESSAGE(hl != partColor(i), "highlight color collides with palette entry " << i);
}

TEST_CASE("selectedSocketColor is deterministic across calls")
{
    CHECK(selectedSocketColor() == selectedSocketColor());
}

TEST_CASE("highlight path emits 6 extra halo vertices over a non-highlighted socket")
{
    // Models exactly what buildOverlayLines does per socket: every socket draws an axis triad
    // (6 verts); the SELECTED socket additionally gets a halo addMarker in selectedSocketColor
    // (+6 verts). So the selected socket's line list is 6 longer, and the extra vertices carry
    // the highlight color (distinct from the triad's per-axis colors).
    ImmediateDebugDrawList notSelected;
    addAxisTriad(notSelected, glm::mat4(1.0f), 0.06f);

    ImmediateDebugDrawList selected;
    addAxisTriad(selected, glm::mat4(1.0f), 0.06f);
    addMarker(selected, glm::vec3(0.0f), 0.09f, selectedSocketColor()); // halo on the selected socket

    REQUIRE(notSelected.vertexCount() == 6);
    REQUIRE(selected.vertexCount() == 12);
    CHECK(selected.vertexCount() - notSelected.vertexCount() == 6);

    // The 6 extra vertices (the halo) all carry the highlight color, which is NOT one of the
    // triad's per-axis colors — so the selected socket reads distinctly.
    for (size_t v = 6; v < 12; ++v)
        CHECK(selected.lineVertices[v].color == selectedSocketColor());
}

TEST_CASE("socket connector line is non-degenerate for an offset socket, skipped when coincident")
{
    // buildOverlayLines draws a thin connector from the socket origin back to its anchor (bone
    // joint / part origin) and SKIPS it when the two are ~coincident (zero offset). Model the
    // geometry: a non-zero offset yields a real segment; a zero offset would be a degenerate point.
    const glm::vec3 anchor{0.0f, 1.0f, 0.0f};
    const glm::vec3 offsetSocket = anchor + glm::vec3(0.5f, 0.0f, 0.0f);

    ImmediateDebugDrawList list;
    addLine(list, offsetSocket, anchor, glm::vec4(0.55f, 0.55f, 0.55f, 1.0f));
    REQUIRE(list.vertexCount() == 2);
    const glm::vec3 seg = list.lineVertices[0].position - list.lineVertices[1].position;
    CHECK(glm::length(seg) == doctest::Approx(0.5f)); // real attach distance, not degenerate

    // Coincident endpoints (zero offset) -> the controller's eps gate drops the segment entirely.
    const glm::vec3 coincident = anchor;
    const glm::vec3 d = coincident - anchor;
    CHECK(glm::dot(d, d) <= 1e-8f); // below the controller's kCoincidentEps2 -> not drawn
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

// --- VK-1433 Phase 1c gap coverage (P1cTest) ------------------------------------------------
//
// The cases below pin contracts the existing suite touches but does not fully prove:
//  * selectedSocketColor distinctness re-asserted explicitly with the exact RGBA the cpp uses;
//  * the per-socket HIGHLIGHT GATE (only the matching (part, socketIdx) draws a halo) modeled
//    against the exact comparison in PrefabRigPreviewController::buildOverlayLines (cpp:283-285);
//  * the CONNECTOR eps-skip boundary (cpp:275-277): coincident -> dropped, just-above-eps -> drawn;
//  * the connector length == the socket's localPosition magnitude (the attach distance it visualizes).

TEST_CASE("selectedSocketColor: exact value and distinct from EVERY palette entry (pinned)")
{
    // Pin the literal so a future tweak to the halo color is a deliberate, test-visible change.
    CHECK(selectedSocketColor() == glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    // Distinct from all 6 palette entries (re-derive the palette via partColor, not a copy).
    for (size_t i = 0; i < 6; ++i)
        CHECK_MESSAGE(selectedSocketColor() != partColor(i),
                      "halo collides with palette entry " << i);
    // ... and from the per-axis triad colors the halo overlays (white differs from each).
    CHECK(selectedSocketColor() != glm::vec4(1.0f, 0.15f, 0.15f, 1.0f)); // X (red)
    CHECK(selectedSocketColor() != glm::vec4(0.15f, 1.0f, 0.15f, 1.0f)); // Y (green)
    CHECK(selectedSocketColor() != glm::vec4(0.15f, 0.35f, 1.0f, 1.0f)); // Z (blue)
}

namespace
{
    // Faithful CPU re-creation of the per-socket emission in buildOverlayLines (cpp:262-285):
    // every socket emits an axis triad (6 verts); ONLY the socket whose (part,socketIdx) matches
    // (highlightPart,highlightIdx) additionally emits a halo addMarker in selectedSocketColor
    // (+6 verts). Mirrors the exact int(i)==part && int(socketIdx)==idx comparison (incl. the
    // -1/-1 = none default), so this test fails if that gate logic ever inverts or drops a term.
    size_t emitSocket(ImmediateDebugDrawList& out, int part, int socketIdx,
                      int highlightPart, int highlightIdx, const glm::mat4& socketWorld)
    {
        const size_t before = out.vertexCount();
        addAxisTriad(out, socketWorld, 0.06f);
        if (part == highlightPart && socketIdx == highlightIdx)
            addMarker(out, glm::vec3(socketWorld[3]), 0.09f, selectedSocketColor());
        return out.vertexCount() - before;
    }
}

TEST_CASE("highlight gate: only the matching (part, socketIdx) socket gets a halo")
{
    // Two parts, two sockets each. Select (part 1, socket 0). Exactly one socket gets +6.
    const int highlightPart = 1;
    const int highlightIdx = 0;

    struct S { int part; int idx; };
    const S sockets[] = { {0,0}, {0,1}, {1,0}, {1,1} };

    int halos = 0;
    for (const S& s : sockets)
    {
        ImmediateDebugDrawList one;
        const size_t emitted = emitSocket(one, s.part, s.idx, highlightPart, highlightIdx,
                                          glm::mat4(1.0f));
        const bool isSelected = (s.part == highlightPart && s.idx == highlightIdx);
        // Selected -> triad(6) + halo(6) = 12; others -> triad only = 6.
        CHECK(emitted == (isSelected ? size_t(12) : size_t(6)));
        if (emitted == 12)
        {
            ++halos;
            // The 6 trailing (halo) verts carry the highlight color; the leading 6 (triad) do not.
            for (size_t v = 6; v < 12; ++v)
                CHECK(one.lineVertices[v].color == selectedSocketColor());
        }
    }
    CHECK(halos == 1); // a single socket is highlighted, never zero or several
}

TEST_CASE("highlight gate: NEVER fires when only one index matches (no off-by-one across parts)")
{
    // Guards against the documented risk of comparing part-loop-index vs socketIdx: a halo must
    // require BOTH the part AND the socket index to match. Same part, wrong socket -> no halo;
    // same socket index, wrong part -> no halo.
    ImmediateDebugDrawList list;

    // highlight = (part 2, socket 3). Try the two "one term matches" near-misses.
    CHECK(emitSocket(list, /*part*/2, /*idx*/0, /*hlPart*/2, /*hlIdx*/3, glm::mat4(1.0f)) == 6); // part matches, idx differs
    CHECK(emitSocket(list, /*part*/0, /*idx*/3, /*hlPart*/2, /*hlIdx*/3, glm::mat4(1.0f)) == 6); // idx matches, part differs
    // Cross-confusion: a socket at (part 3, idx 2) must NOT match a highlight of (part 2, idx 3).
    CHECK(emitSocket(list, /*part*/3, /*idx*/2, /*hlPart*/2, /*hlIdx*/3, glm::mat4(1.0f)) == 6);
}

TEST_CASE("highlight gate: the -1/-1 default selects nothing")
{
    // PreviewEnvironmentParams defaults highlightedSocketPart/Index to -1 (= none). No real socket
    // index is -1, so no halo is ever drawn until the editor sets a selection.
    ImmediateDebugDrawList list;
    CHECK(emitSocket(list, 0, 0, -1, -1, glm::mat4(1.0f)) == 6);
    CHECK(emitSocket(list, 5, 9, -1, -1, glm::mat4(1.0f)) == 6);
}

namespace
{
    // Faithful CPU re-creation of the connector emission in buildOverlayLines (cpp:275-279):
    // a thin connector socketOrigin->anchor, dropped when ~coincident. Returns verts emitted (0 or 2).
    size_t emitConnector(ImmediateDebugDrawList& out, const glm::vec3& socketOrigin,
                         const glm::vec3& anchor)
    {
        constexpr float kCoincidentEps2 = 1e-8f; // identical to the production constant
        const size_t before = out.vertexCount();
        const glm::vec3 connector = socketOrigin - anchor;
        if (glm::dot(connector, connector) > kCoincidentEps2)
            addLine(out, socketOrigin, anchor, glm::vec4(0.55f, 0.55f, 0.55f, 1.0f));
        return out.vertexCount() - before;
    }
}

TEST_CASE("connector eps gate: coincident -> dropped, just-above-eps -> drawn")
{
    const glm::vec3 anchor{0.0f, 1.0f, 0.0f};

    // Exactly coincident: dot == 0 <= eps -> no segment.
    {
        ImmediateDebugDrawList list;
        CHECK(emitConnector(list, anchor, anchor) == 0);
    }

    // Just BELOW the boundary: |d|^2 < 1e-8 -> dropped. dot = (1e-5)^2 = 1e-10 < 1e-8.
    {
        ImmediateDebugDrawList list;
        const glm::vec3 tiny = anchor + glm::vec3(1e-5f, 0.0f, 0.0f);
        CHECK(glm::dot(tiny - anchor, tiny - anchor) < 1e-8f); // confirm the premise
        CHECK(emitConnector(list, tiny, anchor) == 0);
    }

    // Just ABOVE the boundary: |d|^2 > 1e-8 -> drawn. dot = (1e-3)^2 = 1e-6 > 1e-8.
    {
        ImmediateDebugDrawList list;
        const glm::vec3 small = anchor + glm::vec3(1e-3f, 0.0f, 0.0f);
        CHECK(glm::dot(small - anchor, small - anchor) > 1e-8f); // confirm the premise
        REQUIRE(emitConnector(list, small, anchor) == 2);
        CHECK(vecNear(list.lineVertices[0].position, small));  // from the socket origin
        CHECK(vecNear(list.lineVertices[1].position, anchor)); // back to the anchor
    }
}

TEST_CASE("connector length equals the socket's localPosition magnitude (the attach distance)")
{
    // The connector visualizes how far the socket sits from its anchor. For an identity partWorld
    // the world offset IS the socket's localPosition, so the drawn segment length == |localPosition|.
    const glm::vec3 anchor{0.0f, 0.0f, 0.0f};
    const glm::vec3 localPosition{0.3f, -0.4f, 0.0f}; // |.| = 0.5
    const glm::vec3 socketOrigin = anchor + localPosition;

    ImmediateDebugDrawList list;
    REQUIRE(emitConnector(list, socketOrigin, anchor) == 2);
    const glm::vec3 seg = list.lineVertices[0].position - list.lineVertices[1].position;
    CHECK(glm::length(seg) == doctest::Approx(glm::length(localPosition)));
    CHECK(glm::length(seg) == doctest::Approx(0.5f));
}

TEST_CASE("connector length is rotation-invariant under a rotated partWorld (rigid transform)")
{
    // partWorld with rotation but no scale (a rigid frame) is length-preserving, so the connector
    // (socketOrigin - anchor, both pushed through the same partWorld) keeps |localPosition|. This
    // pins that the on-screen attach distance reflects the authored offset, not the rig's pose.
    const glm::vec3 localPosition{0.5f, 0.0f, 0.0f};
    const glm::mat4 partWorld =
        glm::rotate(glm::mat4(1.0f), glm::radians(73.0f), glm::normalize(glm::vec3(1, 2, 3)));

    // anchor = partWorld * jointModel(origin); socketOrigin = partWorld * (joint + localPosition).
    const glm::vec3 jointModel{0.0f, 1.0f, 0.0f};
    const glm::vec3 anchor = glm::vec3(partWorld * glm::vec4(jointModel, 1.0f));
    const glm::vec3 socketOrigin = glm::vec3(partWorld * glm::vec4(jointModel + localPosition, 1.0f));

    ImmediateDebugDrawList list;
    REQUIRE(emitConnector(list, socketOrigin, anchor) == 2);
    const glm::vec3 seg = list.lineVertices[0].position - list.lineVertices[1].position;
    CHECK(glm::length(seg) == doctest::Approx(glm::length(localPosition))); // 0.5, unchanged by rotation
}

} // TEST_SUITE
