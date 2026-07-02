#include <doctest.h>
#include <render/shadow/ShadowPageOverlap.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// ============================================================
// ShadowPageOverlap::overlappedPages — projects a bounding box into a shadow view's
// page grid and returns the inclusive page range it touches. This is the keystone the
// per-page dynamic marking (A1) and the page-binned shadow cull (B1) both build on, and
// it is mirrored statement-for-statement in resources/shaders/common/shadow_page_overlap.glsl.
//
// The strongest guard is parity vs. an independent brute-force: densely sample the box
// surface, project each sample, map to a page, and box the result. For an orthographic
// clipmap (affine) and for an all-in-front perspective view, the projected hull is convex
// and its extremes are at the corners, so the corner-derived range must equal the dense
// surface-sampled range.
// ============================================================

using render::shadow::ShadowPageOverlap;
using render::shadow::PageRange;

namespace
{
    // Orthographic clipmap-style VP: maps world xy in [-extent, extent] to NDC [-1, 1],
    // looking straight down -Z (matches DirectionalShadowCalculator's Vulkan Y-flip).
    glm::mat4 orthoVP(float extent)
    {
        glm::mat4 proj = glm::ortho(-extent, extent, -extent, extent, 0.0f, 1000.0f);
        proj[1][1] *= -1.0f;
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 100.0f),
                                     glm::vec3(0.0f, 0.0f, 0.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f));
        return proj * view;
    }

    // Perspective VP looking down -Z from +Z; near plane at z = eye - 0.1.
    glm::mat4 perspVP(const glm::vec3& eye)
    {
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
        proj[1][1] *= -1.0f;
        glm::mat4 view = glm::lookAt(eye, eye + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        return proj * view;
    }

    struct RefRange
    {
        int fx0 = 1 << 30, fy0 = 1 << 30, fx1 = -1, fy1 = -1;
        bool anyInside = false;
        bool anyBehind = false;
    };

    // Independent reference: sample the six faces of the box on an N x N grid, project each
    // sample, and accumulate the in-grid page bounding box. Also reports whether any sample
    // fell behind the near plane (w <= eps).
    RefRange bruteForce(const glm::vec3& mn, const glm::vec3& mx, const glm::mat4& vp,
                        uint32_t pagesX, uint32_t pagesY)
    {
        RefRange r;
        const int N = 16;
        auto addSample = [&](const glm::vec3& p)
        {
            glm::vec4 clip = vp * glm::vec4(p, 1.0f);
            if (clip.w <= 1e-6f) { r.anyBehind = true; return; }
            float nx = clip.x / clip.w;
            float ny = clip.y / clip.w;
            if (nx < -1.0f || nx > 1.0f || ny < -1.0f || ny > 1.0f) return;
            int px = static_cast<int>(std::floor((nx * 0.5f + 0.5f) * pagesX));
            int py = static_cast<int>(std::floor((ny * 0.5f + 0.5f) * pagesY));
            px = std::min(std::max(px, 0), static_cast<int>(pagesX) - 1);
            py = std::min(std::max(py, 0), static_cast<int>(pagesY) - 1);
            r.fx0 = std::min(r.fx0, px); r.fx1 = std::max(r.fx1, px);
            r.fy0 = std::min(r.fy0, py); r.fy1 = std::max(r.fy1, py);
            r.anyInside = true;
        };
        // Iterate the 3 axis-aligned face pairs.
        for (int axis = 0; axis < 3; ++axis)
            for (int side = 0; side < 2; ++side)
                for (int i = 0; i <= N; ++i)
                    for (int j = 0; j <= N; ++j)
                    {
                        float u = static_cast<float>(i) / N;
                        float v = static_cast<float>(j) / N;
                        glm::vec3 p;
                        float fixed = (side == 0) ? mn[axis] : mx[axis];
                        int a = (axis + 1) % 3, b = (axis + 2) % 3;
                        p[axis] = fixed;
                        p[a] = glm::mix(mn[a], mx[a], u);
                        p[b] = glm::mix(mn[b], mx[b], v);
                        addSample(p);
                    }
        return r;
    }
}

TEST_CASE("overlappedPages: centered small box lands on the center pages")
{
    // 8x8 grid over world [-32,32]. A tiny box at the origin should hit only the two center
    // pages straddling NDC 0 (pages 3 and 4 both, since a point exactly at 0 lands in page 4).
    glm::mat4 vp = orthoVP(32.0f);
    PageRange r = ShadowPageOverlap::overlappedPages(
        glm::vec3(-0.5f, -0.5f, -1.0f), glm::vec3(0.5f, 0.5f, 1.0f), vp, 8, 8);
    CHECK(r.valid);
    CHECK(r.fx0 <= 4u);
    CHECK(r.fx1 >= 3u);
    CHECK(r.fy0 <= 4u);
    CHECK(r.fy1 >= 3u);
    // A half-metre box out of a 64 m span must not spill across the whole grid.
    CHECK(r.fx1 - r.fx0 <= 1u);
    CHECK(r.fy1 - r.fy0 <= 1u);
}

TEST_CASE("overlappedPages: box covering the whole view spans the whole grid")
{
    glm::mat4 vp = orthoVP(32.0f);
    PageRange r = ShadowPageOverlap::overlappedPages(
        glm::vec3(-40.0f, -40.0f, -1.0f), glm::vec3(40.0f, 40.0f, 1.0f), vp, 6, 6);
    CHECK(r.valid);
    CHECK(r.fx0 == 0u);
    CHECK(r.fy0 == 0u);
    CHECK(r.fx1 == 5u);
    CHECK(r.fy1 == 5u);
}

TEST_CASE("overlappedPages: box fully outside the view projects to no pages")
{
    glm::mat4 vp = orthoVP(32.0f);
    // Far to the +X side, well outside [-32,32].
    PageRange r = ShadowPageOverlap::overlappedPages(
        glm::vec3(100.0f, -1.0f, -1.0f), glm::vec3(120.0f, 1.0f, 1.0f), vp, 8, 8);
    CHECK_FALSE(r.valid);
}

TEST_CASE("overlappedPages: parity vs brute-force surface sampling (orthographic)")
{
    const uint32_t grids[][2] = {{2, 2}, {4, 4}, {6, 6}, {8, 8}, {6, 24}};
    const glm::mat4 vp = orthoVP(50.0f);

    // A spread of boxes: centered, off-center, elongated, boundary-straddling.
    const glm::vec3 boxes[][2] = {
        {{-5, -5, -2}, {5, 5, 2}},
        {{10, -20, -2}, {25, -5, 2}},
        {{-49, -3, -1}, {49, 3, 1}},
        {{-2, 30, -1}, {2, 49, 1}},
        {{-33.34f, 8.1f, -1}, {-16.6f, 24.9f, 1}},
    };

    for (auto& g : grids)
        for (auto& b : boxes)
        {
            PageRange got = ShadowPageOverlap::overlappedPages(b[0], b[1], vp, g[0], g[1]);
            RefRange ref = bruteForce(b[0], b[1], vp, g[0], g[1]);
            REQUIRE(ref.anyBehind == false); // ortho: nothing behind
            CHECK(got.valid == ref.anyInside);
            if (got.valid && ref.anyInside)
            {
                CHECK(static_cast<int>(got.fx0) == ref.fx0);
                CHECK(static_cast<int>(got.fx1) == ref.fx1);
                CHECK(static_cast<int>(got.fy0) == ref.fy0);
                CHECK(static_cast<int>(got.fy1) == ref.fy1);
            }
        }
}

TEST_CASE("overlappedPages: parity vs brute-force (perspective, fully in front)")
{
    // Camera/light at +Z looking down -Z; boxes placed well in front so no near-plane crossing.
    const glm::mat4 vp = perspVP(glm::vec3(0.0f, 0.0f, 20.0f));
    const uint32_t px = 8, py = 8;
    const glm::vec3 boxes[][2] = {
        {{-2, -2, -5}, {2, 2, -1}},
        {{-6, 1, -10}, {-2, 5, -6}},
    };
    for (auto& b : boxes)
    {
        RefRange ref = bruteForce(b[0], b[1], vp, px, py);
        REQUIRE(ref.anyBehind == false);
        PageRange got = ShadowPageOverlap::overlappedPages(b[0], b[1], vp, px, py);
        CHECK(got.valid == ref.anyInside);
        if (got.valid && ref.anyInside)
        {
            CHECK(static_cast<int>(got.fx0) == ref.fx0);
            CHECK(static_cast<int>(got.fx1) == ref.fx1);
            CHECK(static_cast<int>(got.fy0) == ref.fy0);
            CHECK(static_cast<int>(got.fy1) == ref.fy1);
        }
    }
}

TEST_CASE("overlappedPages: near-plane straddle falls back to the full grid")
{
    // Box spanning from behind to in front of the light near plane (perspective): the projected
    // hull is unreliable, so the conservative result is the whole grid.
    const glm::mat4 vp = perspVP(glm::vec3(0.0f, 0.0f, 0.0f)); // near plane at z = -0.1
    const glm::vec3 mn(-1.0f, -1.0f, -5.0f), mx(1.0f, 1.0f, 5.0f); // spans z from in-front to behind
    PageRange r = ShadowPageOverlap::overlappedPages(mn, mx, vp, 8, 8);
    RefRange ref = bruteForce(mn, mx, vp, 8, 8);
    REQUIRE(ref.anyBehind == true);
    CHECK(r.valid);
    CHECK(r.fx0 == 0u);
    CHECK(r.fy0 == 0u);
    CHECK(r.fx1 == 7u);
    CHECK(r.fy1 == 7u);
}

TEST_CASE("overlappedPages: degenerate grid dimensions are rejected")
{
    glm::mat4 vp = orthoVP(32.0f);
    PageRange r = ShadowPageOverlap::overlappedPages(
        glm::vec3(-1.0f), glm::vec3(1.0f), vp, 0, 8);
    CHECK_FALSE(r.valid);
}
