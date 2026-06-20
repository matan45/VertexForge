#include <doctest.h>
#include <math/ScreenRegionFrustum.hpp>
#include <math/Frustum.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ============================================================
// Phase 2: RuntimePicker::pickRegion frustum math (RTS drag-select).
//
// Pure CPU math — no Vulkan/GLFW. Validates that
// math::buildScreenRegionFrustum + classifyPointInFrustum:
//   * accept points clearly inside the screen sub-rect frustum,
//   * reject points behind the camera / outside the box,
//   * normalize inverted (max<min) drags,
//   * handle a degenerate zero-area rect.
//
// The matrices mirror the engine: GLM_FORCE_DEPTH_ZERO_TO_ONE is defined
// engine-wide so glm::perspective produces the Vulkan [0,1] depth range that
// Frustum.hpp's plane extraction assumes.
// ============================================================

namespace
{
    constexpr float kW = 800.0f;
    constexpr float kH = 600.0f;

    // Camera at (0,0,5) looking down -z, up +y. World origin is dead-center.
    glm::mat4 testView()
    {
        return glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f),
                           glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::mat4 testProj()
    {
        // 60deg vertical FOV, 4:3, near 0.1, far 100. Half-extent at the z=0
        // plane (distance 5) is ~5*tan(30deg) ~= 2.89 horizontally.
        return glm::perspective(glm::radians(60.0f), kW / kH, 0.1f, 100.0f);
    }
}

TEST_SUITE("PickRegion") {

TEST_CASE("full-screen rect accepts a point in front and rejects one behind") {
    const math::Frustum f = math::buildScreenRegionFrustum(
        testView(), testProj(), glm::vec2(0.0f, 0.0f), glm::vec2(kW, kH), kW, kH);

    // World origin sits in front of the camera, centered.
    CHECK(math::classifyPointInFrustum(f, glm::vec3(0.0f, 0.0f, 0.0f)));

    // Behind the camera (camera is at z=5 looking -z, so z=+10 is behind).
    CHECK_FALSE(math::classifyPointInFrustum(f, glm::vec3(0.0f, 0.0f, 10.0f)));

    // Far outside the horizontal FOV at the focus plane.
    CHECK_FALSE(math::classifyPointInFrustum(f, glm::vec3(100.0f, 0.0f, 0.0f)));
    CHECK_FALSE(math::classifyPointInFrustum(f, glm::vec3(-100.0f, 0.0f, 0.0f)));
    // Far outside vertically.
    CHECK_FALSE(math::classifyPointInFrustum(f, glm::vec3(0.0f, 100.0f, 0.0f)));
    CHECK_FALSE(math::classifyPointInFrustum(f, glm::vec3(0.0f, -100.0f, 0.0f)));
}

TEST_CASE("right-half rect splits left vs right world points") {
    // A point at +x view-space projects to the right of screen (high pixel x),
    // -x to the left. So the right half-rect must contain +x and exclude -x.
    const math::Frustum right = math::buildScreenRegionFrustum(
        testView(), testProj(), glm::vec2(kW * 0.5f, 0.0f), glm::vec2(kW, kH), kW, kH);

    CHECK(math::classifyPointInFrustum(right, glm::vec3(2.0f, 0.0f, 0.0f)));
    CHECK_FALSE(math::classifyPointInFrustum(right, glm::vec3(-2.0f, 0.0f, 0.0f)));

    const math::Frustum left = math::buildScreenRegionFrustum(
        testView(), testProj(), glm::vec2(0.0f, 0.0f), glm::vec2(kW * 0.5f, kH), kW, kH);

    CHECK(math::classifyPointInFrustum(left, glm::vec3(-2.0f, 0.0f, 0.0f)));
    CHECK_FALSE(math::classifyPointInFrustum(left, glm::vec3(2.0f, 0.0f, 0.0f)));
}

TEST_CASE("inverted drag (max < min) is normalized to the same frustum") {
    // Drag from bottom-right to top-left must give the same result as the
    // canonical min->max corners.
    const math::Frustum canonical = math::buildScreenRegionFrustum(
        testView(), testProj(), glm::vec2(kW * 0.5f, 0.0f), glm::vec2(kW, kH), kW, kH);
    const math::Frustum inverted = math::buildScreenRegionFrustum(
        testView(), testProj(), glm::vec2(kW, kH), glm::vec2(kW * 0.5f, 0.0f), kW, kH);

    const glm::vec3 insidePt(2.0f, 0.0f, 0.0f);
    const glm::vec3 outsidePt(-2.0f, 0.0f, 0.0f);

    CHECK(math::classifyPointInFrustum(canonical, insidePt));
    CHECK(math::classifyPointInFrustum(inverted, insidePt));
    CHECK_FALSE(math::classifyPointInFrustum(canonical, outsidePt));
    CHECK_FALSE(math::classifyPointInFrustum(inverted, outsidePt));
}

TEST_CASE("a small box around screen center selects only the centered point") {
    // 40px box centered on the screen: contains the world origin, excludes a
    // point that projects well off-center.
    const float cx = kW * 0.5f;
    const float cy = kH * 0.5f;
    const math::Frustum f = math::buildScreenRegionFrustum(
        testView(), testProj(), glm::vec2(cx - 20.0f, cy - 20.0f),
        glm::vec2(cx + 20.0f, cy + 20.0f), kW, kH);

    CHECK(math::classifyPointInFrustum(f, glm::vec3(0.0f, 0.0f, 0.0f)));
    CHECK_FALSE(math::classifyPointInFrustum(f, glm::vec3(2.0f, 0.0f, 0.0f)));
    CHECK_FALSE(math::classifyPointInFrustum(f, glm::vec3(-2.0f, 0.0f, 0.0f)));
}

TEST_CASE("degenerate zero-area rect does not crash and selects ~nothing") {
    // Both corners identical -> zero span on both axes. Must not divide by zero;
    // the collapsed frustum should reject normal scene points.
    const math::Frustum f = math::buildScreenRegionFrustum(
        testView(), testProj(), glm::vec2(kW * 0.5f, kH * 0.5f),
        glm::vec2(kW * 0.5f, kH * 0.5f), kW, kH);

    CHECK_FALSE(math::classifyPointInFrustum(f, glm::vec3(2.0f, 0.0f, 0.0f)));
    CHECK_FALSE(math::classifyPointInFrustum(f, glm::vec3(0.0f, 2.0f, 0.0f)));
    CHECK_FALSE(math::classifyPointInFrustum(f, glm::vec3(-2.0f, 0.0f, 0.0f)));
}

TEST_CASE("zero viewport dimensions are guarded (no divide-by-zero)") {
    // Should not crash; result is unspecified but must be a valid frustum.
    const math::Frustum f = math::buildScreenRegionFrustum(
        testView(), testProj(), glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 0.0f), 0.0f, 0.0f);
    (void)math::classifyPointInFrustum(f, glm::vec3(0.0f, 0.0f, 0.0f));
    CHECK(f.isInitialized());
}

} // TEST_SUITE
