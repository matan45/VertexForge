#include <doctest.h>

// VK-1433 Phase 1b — CPU coverage for the header-only bone-pick seam
// windows::prefabrigpick::nearestJointToScreenPoint / projectJointToScreen.
//
// Pure math: no imgui, no Graphics, no Vulkan, no EventDispatcher. The matrices mirror the editor
// camera EXACTLY — OrbitCamera builds glm::perspective(...) then negates proj[1][1] (Vulkan Y-down),
// and the gizmo/pick path undoes that flip internally. So the test feeds a "camera-style" projection
// (perspective with [1][1] negated) and verifies the helper maps a joint ABOVE the rig to the UPPER
// half of the viewport (not mirrored), hits the nearest joint, and rejects behind-camera /
// out-of-viewport / out-of-threshold cases.

#include "windows/preview/PrefabRigBonePick.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <limits>
#include <vector>

using namespace windows::prefabrigpick;

namespace
{
    // Camera looking down -Z at the origin from (0,0,5), up = +Y.
    glm::mat4 makeView()
    {
        return glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Editor-camera-style projection: perspective with the Vulkan Y-flip baked in (== OrbitCamera).
    glm::mat4 makeProj(float aspect = 1.0f)
    {
        glm::mat4 p = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        p[1][1] *= -1.0f;
        return p;
    }

    const glm::vec2 kViewMin{100.0f, 50.0f};
    const glm::vec2 kViewSize{800.0f, 600.0f};
    const glm::vec2 kViewCenter = kViewMin + kViewSize * 0.5f;
}

TEST_CASE("bonepick: joint at origin projects to viewport center")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    glm::vec2 screen{};
    REQUIRE(projectJointToScreen(glm::vec3(0.0f), view, proj, kViewMin, kViewSize, screen));

    // A point on the view axis at the target lands at the viewport center.
    CHECK(screen.x == doctest::Approx(kViewCenter.x).epsilon(0.001));
    CHECK(screen.y == doctest::Approx(kViewCenter.y).epsilon(0.001));
}

TEST_CASE("bonepick: a joint ABOVE the rig projects to the UPPER half (proj[1][1] flip handled)")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    glm::vec2 screen{};
    REQUIRE(projectJointToScreen(glm::vec3(0.0f, 1.0f, 0.0f), view, proj, kViewMin, kViewSize, screen));

    // Centered horizontally, ABOVE center (smaller screen Y = upper half) — NOT mirrored below.
    CHECK(screen.x == doctest::Approx(kViewCenter.x).epsilon(0.001));
    CHECK(screen.y < kViewCenter.y);

    // And a joint BELOW the rig maps below center (sanity on the sign).
    glm::vec2 below{};
    REQUIRE(projectJointToScreen(glm::vec3(0.0f, -1.0f, 0.0f), view, proj, kViewMin, kViewSize, below));
    CHECK(below.y > kViewCenter.y);
}

TEST_CASE("bonepick: a joint directly under the click is hit")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    const std::vector<glm::vec3> joints = {glm::vec3(0.0f)}; // projects to center
    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, kViewCenter, 10.0f);

    CHECK(r.hit());
    CHECK(r.index == 0);
    CHECK(r.screenDistance == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("bonepick: nearest of several joints wins")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    // Three joints spread along +Y; the click is at the center, so the origin joint is nearest.
    const std::vector<glm::vec3> joints = {
        glm::vec3(0.0f, 0.8f, 0.0f),  // upper
        glm::vec3(0.0f, 0.0f, 0.0f),  // center (nearest to a center click)
        glm::vec3(0.0f, -0.8f, 0.0f), // lower
    };

    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, kViewCenter, 1000.0f);
    CHECK(r.hit());
    CHECK(r.index == 1);

    // A click near the UPPER joint's screen position selects index 0 instead.
    glm::vec2 upperScreen{};
    REQUIRE(projectJointToScreen(joints[0], view, proj, kViewMin, kViewSize, upperScreen));
    const JointPickResult r2 =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, upperScreen, 1000.0f);
    CHECK(r2.hit());
    CHECK(r2.index == 0);
}

TEST_CASE("bonepick: behind-camera joint is rejected")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    // Behind the camera (camera at z=5 looking toward -Z; z=10 is behind it).
    glm::vec2 screen{};
    CHECK_FALSE(projectJointToScreen(glm::vec3(0.0f, 0.0f, 10.0f), view, proj, kViewMin, kViewSize, screen));

    const std::vector<glm::vec3> joints = {glm::vec3(0.0f, 0.0f, 10.0f)};
    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, kViewCenter, 1000.0f);
    CHECK_FALSE(r.hit());
    CHECK(r.index == -1);
}

TEST_CASE("bonepick: a joint projecting outside the viewport is rejected")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    // Far off to the side so it projects well outside the 800x600 rect, even though it's in front.
    const std::vector<glm::vec3> joints = {glm::vec3(50.0f, 0.0f, 0.0f)};

    // Confirm it really lands outside the rect.
    glm::vec2 screen{};
    REQUIRE(projectJointToScreen(joints[0], view, proj, kViewMin, kViewSize, screen));
    const bool outside = screen.x < kViewMin.x || screen.x > kViewMin.x + kViewSize.x ||
                         screen.y < kViewMin.y || screen.y > kViewMin.y + kViewSize.y;
    REQUIRE(outside);

    // A center click cannot pick it (huge threshold doesn't override the viewport rejection).
    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, kViewCenter, 100000.0f);
    CHECK_FALSE(r.hit());
}

TEST_CASE("bonepick: nothing within the pixel threshold returns -1")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    const std::vector<glm::vec3> joints = {glm::vec3(0.0f)}; // projects to center
    // Click far from the center but still inside the viewport, with a tiny threshold.
    const glm::vec2 click = kViewMin + glm::vec2(20.0f, 20.0f);
    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, click, 5.0f);
    CHECK_FALSE(r.hit());
    CHECK(r.index == -1);
    CHECK(r.screenDistance == std::numeric_limits<float>::max());
}

TEST_CASE("bonepick: degenerate viewport returns no hit")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();
    const std::vector<glm::vec3> joints = {glm::vec3(0.0f)};

    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, glm::vec2(0.0f, 0.0f), kViewCenter, 10.0f);
    CHECK_FALSE(r.hit());
}

// ---------------------------------------------------------------------------------------------------
// VK-1433 Phase 1b — additional coverage (P5Impl review). Expected pixel values below are DERIVED from
// the projection math (f = 1/tan(30deg) = sqrt(3); view maps (x,y,z) -> (x, y, z-5) in camera space),
// not eyeballed. The camera negates proj[1][1] and the helper negates again, so the net projection is
// the original GL perspective; ndc.x/.y depend only on f and clip.w = -(z-5).
// ---------------------------------------------------------------------------------------------------

TEST_CASE("bonepick: offset (non-origin) viewport includes the offset in screen coords")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    // A panel NOT at the window origin — the projected screen point must be shifted by viewportMin.
    const glm::vec2 offMin{300.0f, 200.0f};
    const glm::vec2 offSize{400.0f, 400.0f};
    const glm::vec2 offCenter = offMin + offSize * 0.5f; // (500, 400)

    // Origin lands at the offset viewport's center, NOT at (0,0) or the previous (100,50) panel center.
    glm::vec2 screen{};
    REQUIRE(projectJointToScreen(glm::vec3(0.0f), view, proj, offMin, offSize, screen));
    CHECK(screen.x == doctest::Approx(offCenter.x).epsilon(0.001)); // 500
    CHECK(screen.y == doctest::Approx(offCenter.y).epsilon(0.001)); // 400

    // A joint to the +X / +Y side maps right-of / above center, and the absolute coords are inside the
    // offset rect (so the offset really was added, not dropped).
    glm::vec2 rightUp{};
    REQUIRE(projectJointToScreen(glm::vec3(0.5f, 0.5f, 0.0f), view, proj, offMin, offSize, rightUp));
    CHECK(rightUp.x > offCenter.x);
    CHECK(rightUp.y < offCenter.y); // higher world Y -> upper half (smaller screen Y)
    CHECK(rightUp.x >= offMin.x);
    CHECK(rightUp.x <= offMin.x + offSize.x);

    // And a click at the offset center picks the origin joint (the pick path uses the same offset map).
    const std::vector<glm::vec3> joints = {glm::vec3(0.0f)};
    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, offMin, offSize, offCenter, 5.0f);
    CHECK(r.hit());
    CHECK(r.index == 0);
}

TEST_CASE("bonepick: pixel threshold is INCLUSIVE (dist == threshold is a hit)")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    // Derived: a joint at world x = 0.125 * 5 / f projects to screen x = center.x + 50 (exactly 50 px
    // from the center horizontally), because ndc.x = f*x/5 = 0.125 and 0.125 * 0.5 * 800 = 50.
    const float f = 1.0f / std::tan(glm::radians(60.0f) * 0.5f);
    const float xFor50px = 0.125f * 5.0f / f;
    const std::vector<glm::vec3> joints = {glm::vec3(xFor50px, 0.0f, 0.0f)};

    // Confirm the projected distance is really 50 px from a center click.
    glm::vec2 screen{};
    REQUIRE(projectJointToScreen(joints[0], view, proj, kViewMin, kViewSize, screen));
    CHECK(glm::distance(screen, kViewCenter) == doctest::Approx(50.0f).epsilon(0.001));

    // threshold == distance -> HIT (code uses `dist <= pixelThreshold`).
    const JointPickResult onBoundary =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, kViewCenter, 50.0f);
    CHECK(onBoundary.hit());
    CHECK(onBoundary.index == 0);
    CHECK(onBoundary.screenDistance == doctest::Approx(50.0f).epsilon(0.001));

    // threshold just below the distance -> MISS (pins the boundary to <=, not <).
    const JointPickResult justUnder =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, kViewCenter, 49.0f);
    CHECK_FALSE(justUnder.hit());
    CHECK(justUnder.index == -1);
}

TEST_CASE("bonepick: all four quadrants map correctly under the proj[1][1] flip")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    glm::vec2 right{}, left{}, up{}, down{};
    REQUIRE(projectJointToScreen(glm::vec3(1.0f, 0.0f, 0.0f), view, proj, kViewMin, kViewSize, right));
    REQUIRE(projectJointToScreen(glm::vec3(-1.0f, 0.0f, 0.0f), view, proj, kViewMin, kViewSize, left));
    REQUIRE(projectJointToScreen(glm::vec3(0.0f, 1.0f, 0.0f), view, proj, kViewMin, kViewSize, up));
    REQUIRE(projectJointToScreen(glm::vec3(0.0f, -1.0f, 0.0f), view, proj, kViewMin, kViewSize, down));

    CHECK(right.x > kViewCenter.x); // +X world -> right of center
    CHECK(left.x < kViewCenter.x);  // -X world -> left of center
    CHECK(up.y < kViewCenter.y);    // +Y world -> UPPER half (smaller screen Y) — the flip contract
    CHECK(down.y > kViewCenter.y);  // -Y world -> lower half

    // The flip is symmetric: left/right and up/down are mirror images about the center axes.
    CHECK((right.x - kViewCenter.x) == doctest::Approx(kViewCenter.x - left.x).epsilon(0.001));
    CHECK((down.y - kViewCenter.y) == doctest::Approx(kViewCenter.y - up.y).epsilon(0.001));
    // Pure-X joints stay on the center horizontal; pure-Y joints stay on the center vertical.
    CHECK(right.y == doctest::Approx(kViewCenter.y).epsilon(0.001));
    CHECK(up.x == doctest::Approx(kViewCenter.x).epsilon(0.001));
}

TEST_CASE("bonepick: empty joint list returns no hit")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    const std::vector<glm::vec3> joints; // empty
    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, kViewCenter, 1000.0f);
    CHECK_FALSE(r.hit());
    CHECK(r.index == -1);
    CHECK(r.screenDistance == std::numeric_limits<float>::max());
}

TEST_CASE("bonepick: a joint exactly on the camera plane (w == 0) is rejected; just in front is kept")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    // Camera eye is at z=5 looking -Z, so clip.w = -(z-5). At z=5 the joint sits ON the camera plane:
    // w == 0, and the code rejects clip.w <= 0 (note <=, so w==0 is NOT pickable, the near-plane edge).
    glm::vec2 onPlane{};
    CHECK_FALSE(projectJointToScreen(glm::vec3(0.0f, 0.0f, 5.0f), view, proj, kViewMin, kViewSize, onPlane));

    // A joint a hair in front of the eye has tiny positive w and DOES project (it is in front).
    glm::vec2 inFront{};
    CHECK(projectJointToScreen(glm::vec3(0.0f, 0.0f, 4.999f), view, proj, kViewMin, kViewSize, inFront));

    // Through the full pick path, the on-plane joint is skipped.
    const std::vector<glm::vec3> joints = {glm::vec3(0.0f, 0.0f, 5.0f)};
    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, kViewCenter, 100000.0f);
    CHECK_FALSE(r.hit());
}

TEST_CASE("bonepick: tie-break is deterministic — first joint wins on equal distance")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    // All three sit on the view axis (different depths) so they project to the EXACT same screen point
    // (the viewport center). A center click is equidistant (0 px) to all of them; the update test is
    // `dist < best.screenDistance` (strict), so the FIRST index encountered wins and later equals don't
    // displace it.
    const std::vector<glm::vec3> joints = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 0.0f, -3.0f),
    };

    // Sanity: confirm they really collapse to the same screen point.
    glm::vec2 s0{}, s1{}, s2{};
    REQUIRE(projectJointToScreen(joints[0], view, proj, kViewMin, kViewSize, s0));
    REQUIRE(projectJointToScreen(joints[1], view, proj, kViewMin, kViewSize, s1));
    REQUIRE(projectJointToScreen(joints[2], view, proj, kViewMin, kViewSize, s2));
    CHECK(s0.x == doctest::Approx(s1.x).epsilon(0.001));
    CHECK(s0.x == doctest::Approx(s2.x).epsilon(0.001));
    CHECK(s0.y == doctest::Approx(s1.y).epsilon(0.001));

    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, kViewCenter, 1000.0f);
    CHECK(r.hit());
    CHECK(r.index == 0); // first index, not 1 or 2
    CHECK(r.screenDistance == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("bonepick: a joint projecting exactly on the viewport edge is INCLUSIVE (kept)")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    // Derived: x = -5/f projects to ndc.x = -1, i.e. screen.x == viewportMin.x exactly (the left edge).
    // The rejection test is `screen.x < viewportMin.x` (strict), so a joint ON the edge is NOT rejected.
    const float f = 1.0f / std::tan(glm::radians(60.0f) * 0.5f);
    const glm::vec3 edgeJoint(-5.0f / f, 0.0f, 0.0f);

    glm::vec2 screen{};
    REQUIRE(projectJointToScreen(edgeJoint, view, proj, kViewMin, kViewSize, screen));
    CHECK(screen.x == doctest::Approx(kViewMin.x).epsilon(0.001)); // exactly the left edge
    CHECK(screen.y == doctest::Approx(kViewCenter.y).epsilon(0.001));

    // A click right at that edge picks the joint (boundary is inclusive, joint not filtered out).
    const std::vector<glm::vec3> joints = {edgeJoint};
    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, screen, 5.0f);
    CHECK(r.hit());
    CHECK(r.index == 0);
}

TEST_CASE("bonepick: NaN joint coordinate yields no hit (no false positive, no crash)")
{
    const glm::mat4 view = makeView();
    const glm::mat4 proj = makeProj();

    // The helper has no explicit NaN guard. With a NaN coord, clip.w <= 0 is false (NaN compares false),
    // so it proceeds, glm::distance returns NaN, and `dist <= pixelThreshold` is false for NaN — so the
    // joint simply never qualifies. This documents the observed semantics: NaN -> no hit, not a crash
    // and not a spurious selection.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<glm::vec3> joints = {glm::vec3(nan, 0.0f, 0.0f), glm::vec3(0.0f)};

    const JointPickResult r =
        nearestJointToScreenPoint(joints, view, proj, kViewMin, kViewSize, kViewCenter, 1000.0f);
    // The valid origin joint (index 1) is still selectable; the NaN joint never wins.
    CHECK(r.hit());
    CHECK(r.index == 1);
}
