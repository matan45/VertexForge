#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<
#include <components/Components.hpp>
#include <ui/UIRectLayout.hpp>
#include <ui/UIRectMath.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

// VK-1659: the shared UI rect math (utilities/ui/UIRectLayout.hpp) — the runtime resolver, the affine-edge
// rewrite that MCP placement and the edit-mode pivot fix build on, the anchor presets, and gluing a child to
// its parent's rect (UI anchors are fractions of the whole canvas, so a child never follows its parent on
// its own).

namespace
{
    using utilities::ui::AffineRect;
    using utilities::ui::HAlign;
    using utilities::ui::PixelRect;
    using utilities::ui::Placement;
    using utilities::ui::RectLayout;
    using utilities::ui::VAlign;

    constexpr float kRefW = 1920.0f;
    constexpr float kRefH = 1080.0f;

    struct Viewport
    {
        float w;
        float h;
    };

    constexpr std::array<Viewport, 5> kViewports{{
        {1920.0f, 1080.0f}, {1280.0f, 720.0f}, {2560.0f, 1080.0f}, {1080.0f, 1920.0f}, {800.0f, 600.0f}}};

    // The canvas scale rule (UIRectMath computeCanvasScale): ScaleWithScreenSize, else ConstantPixelSize (1).
    float canvasScale(const Viewport& viewport, bool scaleWithScreen)
    {
        return scaleWithScreen ? std::min(viewport.w / kRefW, viewport.h / kRefH) : 1.0f;
    }

    // The body of UIRectMath::resolvePixelRect before VK-1659, verbatim.
    PixelRect oldResolvePixelRect(const RectLayout& rectComp, float parentW, float parentH, float scale)
    {
        float anchorLeftPx  = rectComp.anchorMin.x * parentW;
        float anchorRightPx = rectComp.anchorMax.x * parentW;
        float anchorTopPx   = (1.0f - rectComp.anchorMax.y) * parentH;
        float anchorBotPx   = (1.0f - rectComp.anchorMin.y) * parentH;

        float w = (anchorRightPx - anchorLeftPx) + rectComp.sizeDelta.x * scale;
        float h = (anchorBotPx - anchorTopPx) + rectComp.sizeDelta.y * scale;

        float cx = (anchorLeftPx + anchorRightPx) * 0.5f + rectComp.anchoredPosition.x * scale;
        float cy = (anchorTopPx + anchorBotPx) * 0.5f - rectComp.anchoredPosition.y * scale;

        float posX = cx - rectComp.pivot.x * w;
        float posY = cy - rectComp.pivot.y * h;

        return {posX, posY, w, h};
    }

    // Deterministic pseudo-random numbers (LCG), so failures reproduce.
    struct Lcg
    {
        uint32_t state = 0x1659u;

        float next01()
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<float>(state >> 8) / 16777216.0f;
        }

        float range(float lo, float hi)
        {
            return lo + (hi - lo) * next01();
        }
    };

    // Point anchors half the time, stretched otherwise; any pivot.
    RectLayout randomLayout(Lcg& rng)
    {
        float ax0 = rng.range(0.0f, 1.0f);
        float ax1 = rng.next01() < 0.5f ? ax0 : rng.range(0.0f, 1.0f);
        float ay0 = rng.range(0.0f, 1.0f);
        float ay1 = rng.next01() < 0.5f ? ay0 : rng.range(0.0f, 1.0f);

        RectLayout r;
        r.anchorMin = {std::min(ax0, ax1), std::min(ay0, ay1)};
        r.anchorMax = {std::max(ax0, ax1), std::max(ay0, ay1)};
        r.pivot = {rng.range(0.0f, 1.0f), rng.range(0.0f, 1.0f)};
        r.sizeDelta = {rng.range(-200.0f, 400.0f), rng.range(-200.0f, 400.0f)};
        r.anchoredPosition = {rng.range(-500.0f, 500.0f), rng.range(-500.0f, 500.0f)};
        return r;
    }

    // Resolved pixels agree to a hundredth of a pixel (float rounding of a different expression order).
    constexpr float kPixelTolerance = 1e-2f;

    // Not "near": windows.h (reached through spdlog) defines near / far as empty macros.
    bool approxEqual(float actual, float expected, float tolerance = kPixelTolerance)
    {
        return std::fabs(actual - expected) <= tolerance;
    }

    void checkRectNear(const PixelRect& actual, const PixelRect& expected)
    {
        CHECK(approxEqual(actual.x, expected.x));
        CHECK(approxEqual(actual.y, expected.y));
        CHECK(approxEqual(actual.w, expected.w));
        CHECK(approxEqual(actual.h, expected.h));
    }

    void checkRectExact(const PixelRect& actual, const PixelRect& expected)
    {
        CHECK(actual.x == expected.x);
        CHECK(actual.y == expected.y);
        CHECK(actual.w == expected.w);
        CHECK(actual.h == expected.h);
    }

    Placement makePlacement(HAlign h, VAlign v, glm::vec2 size, glm::vec4 margin = glm::vec4(0.0f),
                            glm::vec2 offset = glm::vec2(0.0f))
    {
        Placement p;
        p.h = h;
        p.v = v;
        p.size = size;
        p.margin = margin;
        p.offset = offset;
        return p;
    }

    PixelRect resolveAt(const RectLayout& layout, const Viewport& viewport, float scale)
    {
        return utilities::ui::resolveRect(layout, viewport.w, viewport.h, scale);
    }

    // A few parents covering point anchors, stretched anchors and off-centre pivots.
    std::array<RectLayout, 4> sampleParents()
    {
        std::array<RectLayout, 4> parents;

        // HUD-style point anchor at the top-left, centre pivot.
        parents[0].anchorMin = parents[0].anchorMax = {0.0f, 1.0f};
        parents[0].sizeDelta = {400.0f, 300.0f};
        parents[0].anchoredPosition = {224.0f, -174.0f};

        // Centred panel.
        parents[1].anchorMin = parents[1].anchorMax = {0.5f, 0.5f};
        parents[1].sizeDelta = {600.0f, 400.0f};

        // Stretched panel with an inset.
        parents[2].anchorMin = {0.1f, 0.2f};
        parents[2].anchorMax = {0.9f, 0.7f};
        parents[2].sizeDelta = {-40.0f, -20.0f};
        parents[2].anchoredPosition = {12.0f, -8.0f};

        // Off-centre pivot (authored outside MCP, e.g. in the inspector).
        parents[3].anchorMin = {0.2f, 0.3f};
        parents[3].anchorMax = {0.7f, 0.9f};
        parents[3].pivot = {0.3f, 0.6f};
        parents[3].sizeDelta = {40.0f, -20.0f};
        parents[3].anchoredPosition = {15.0f, -12.0f};

        return parents;
    }
}

TEST_SUITE("UIRectLayout")
{
    TEST_CASE("resolveRect is the pre-VK-1659 resolvePixelRect, bit for bit")
    {
        Lcg rng;
        for (int i = 0; i < 200; ++i)
        {
            const RectLayout layout = randomLayout(rng);
            for (const Viewport& viewport : kViewports)
            {
                for (bool scaleWithScreen : {true, false})
                {
                    const float s = canvasScale(viewport, scaleWithScreen);
                    const PixelRect actual = utilities::ui::resolveRect(layout, viewport.w, viewport.h, s);
                    checkRectExact(actual, oldResolvePixelRect(layout, viewport.w, viewport.h, s));

                    components::UIRectComponent rect;
                    utilities::ui::applyRectLayout(rect, layout);
                    checkRectExact(utilities::ui::resolvePixelRect(rect, viewport.w, viewport.h, s), actual);
                }
            }
        }
    }

    TEST_CASE("toRectLayout / applyRectLayout copy the five layout fields and keep blocksRaycast")
    {
        components::UIRectComponent rect;
        rect.blocksRaycast = false;
        RectLayout layout;
        layout.anchorMin = {0.1f, 0.2f};
        layout.anchorMax = {0.3f, 0.4f};
        layout.pivot = {0.0f, 1.0f};
        layout.sizeDelta = {50.0f, 60.0f};
        layout.anchoredPosition = {-7.0f, 9.0f};

        utilities::ui::applyRectLayout(rect, layout);
        CHECK(rect.blocksRaycast == false);

        const RectLayout back = utilities::ui::toRectLayout(rect);
        CHECK(back.anchorMin == layout.anchorMin);
        CHECK(back.anchorMax == layout.anchorMax);
        CHECK(back.pivot == layout.pivot);
        CHECK(back.sizeDelta == layout.sizeDelta);
        CHECK(back.anchoredPosition == layout.anchoredPosition);
    }

    TEST_CASE("affine edges evaluate to the resolved rect at every viewport and scale")
    {
        Lcg rng;
        for (int i = 0; i < 200; ++i)
        {
            const RectLayout layout = randomLayout(rng);
            const AffineRect edges = utilities::ui::affineRectOf(layout);
            for (const Viewport& viewport : kViewports)
            {
                for (bool scaleWithScreen : {true, false})
                {
                    const float s = canvasScale(viewport, scaleWithScreen);
                    checkRectNear(utilities::ui::evaluate(edges, viewport.w, viewport.h, s),
                                  resolveAt(layout, viewport, s));
                }
            }
        }
    }

    TEST_CASE("rectFromEdges reproduces the edges with pivot 0.5")
    {
        Lcg rng;
        for (int i = 0; i < 200; ++i)
        {
            const RectLayout layout = randomLayout(rng);
            const AffineRect edges = utilities::ui::affineRectOf(layout);
            const RectLayout rebuilt = utilities::ui::rectFromEdges(edges);

            CHECK(rebuilt.pivot == glm::vec2(0.5f, 0.5f));

            const AffineRect again = utilities::ui::affineRectOf(rebuilt);
            CHECK(approxEqual(again.left.f, edges.left.f, 1e-5f));
            CHECK(approxEqual(again.left.c, edges.left.c, 1e-3f));
            CHECK(approxEqual(again.right.f, edges.right.f, 1e-5f));
            CHECK(approxEqual(again.right.c, edges.right.c, 1e-3f));
            CHECK(approxEqual(again.top.f, edges.top.f, 1e-5f));
            CHECK(approxEqual(again.top.c, edges.top.c, 1e-3f));
            CHECK(approxEqual(again.bottom.f, edges.bottom.f, 1e-5f));
            CHECK(approxEqual(again.bottom.c, edges.bottom.c, 1e-3f));

            for (const Viewport& viewport : kViewports)
            {
                for (bool scaleWithScreen : {true, false})
                {
                    const float s = canvasScale(viewport, scaleWithScreen);
                    checkRectNear(resolveAt(rebuilt, viewport, s), resolveAt(layout, viewport, s));
                }
            }
        }
    }

    TEST_CASE("the 16 anchor presets place a child where their names say")
    {
        std::set<std::string> names;
        for (const auto& preset : utilities::ui::anchorPresets)
        {
            names.insert(std::string(preset.name));
        }
        CHECK(names.size() == utilities::ui::anchorPresets.size());
        CHECK(utilities::ui::anchorPresets.front().name == std::string_view("topLeft"));
        CHECK(utilities::ui::anchorPresets.back().name == std::string_view("stretch"));

        // Margins left 10, right 20, top 30, bottom 40; size 100 x 60; the whole canvas at reference size.
        const AffineRect canvas = utilities::ui::fullViewportRect();
        const glm::vec4 margin(10.0f, 20.0f, 30.0f, 40.0f);
        const Viewport reference{kRefW, kRefH};

        for (const auto& preset : utilities::ui::anchorPresets)
        {
            CAPTURE(std::string(preset.name));
            const RectLayout layout =
                utilities::ui::placeInside(canvas, makePlacement(preset.h, preset.v, {100.0f, 60.0f}, margin));
            CHECK(layout.pivot == glm::vec2(0.5f, 0.5f));
            const PixelRect r = resolveAt(layout, reference, 1.0f);

            switch (preset.h)
            {
            case HAlign::Left:    CHECK(approxEqual(r.x, 10.0f));   CHECK(approxEqual(r.w, 100.0f));  break;
            case HAlign::Center:  CHECK(approxEqual(r.x, 910.0f));  CHECK(approxEqual(r.w, 100.0f));  break;
            case HAlign::Right:   CHECK(approxEqual(r.x, 1800.0f)); CHECK(approxEqual(r.w, 100.0f));  break;
            case HAlign::Stretch: CHECK(approxEqual(r.x, 10.0f));   CHECK(approxEqual(r.w, 1890.0f)); break;
            }
            switch (preset.v)
            {
            case VAlign::Top:     CHECK(approxEqual(r.y, 30.0f));  CHECK(approxEqual(r.h, 60.0f));   break;
            case VAlign::Middle:  CHECK(approxEqual(r.y, 510.0f)); CHECK(approxEqual(r.h, 60.0f));   break;
            case VAlign::Bottom:  CHECK(approxEqual(r.y, 980.0f)); CHECK(approxEqual(r.h, 60.0f));   break;
            case VAlign::Stretch: CHECK(approxEqual(r.y, 30.0f));  CHECK(approxEqual(r.h, 1010.0f)); break;
            }
        }
    }

    TEST_CASE("a placed child stays glued to its parent at every viewport and scale")
    {
        for (const RectLayout& parent : sampleParents())
        {
            const AffineRect parentEdges = utilities::ui::affineRectOf(parent);

            const RectLayout bottomRight = utilities::ui::placeInside(
                parentEdges, makePlacement(HAlign::Right, VAlign::Bottom, {50.0f, 30.0f}, {0.0f, 7.0f, 0.0f, 9.0f}));
            const RectLayout topLeft = utilities::ui::placeInside(
                parentEdges, makePlacement(HAlign::Left, VAlign::Top, {40.0f, 20.0f}, {5.0f, 0.0f, 6.0f, 0.0f}));
            const RectLayout centred = utilities::ui::placeInside(
                parentEdges, makePlacement(HAlign::Center, VAlign::Middle, {80.0f, 24.0f}));
            const RectLayout filled = utilities::ui::placeInside(
                parentEdges, makePlacement(HAlign::Stretch, VAlign::Stretch, {0.0f, 0.0f}, glm::vec4(4.0f)));
            const RectLayout nudged = utilities::ui::placeInside(
                parentEdges, makePlacement(HAlign::Left, VAlign::Top, {40.0f, 20.0f}, {5.0f, 0.0f, 6.0f, 0.0f},
                                           {12.0f, 8.0f}));

            for (const Viewport& viewport : kViewports)
            {
                for (bool scaleWithScreen : {true, false})
                {
                    const float s = canvasScale(viewport, scaleWithScreen);
                    const PixelRect p = resolveAt(parent, viewport, s);

                    const PixelRect br = resolveAt(bottomRight, viewport, s);
                    CHECK(approxEqual(br.x + br.w, p.x + p.w - 7.0f * s));
                    CHECK(approxEqual(br.y + br.h, p.y + p.h - 9.0f * s));
                    CHECK(approxEqual(br.w, 50.0f * s));
                    CHECK(approxEqual(br.h, 30.0f * s));

                    const PixelRect tl = resolveAt(topLeft, viewport, s);
                    CHECK(approxEqual(tl.x, p.x + 5.0f * s));
                    CHECK(approxEqual(tl.y, p.y + 6.0f * s));
                    CHECK(approxEqual(tl.w, 40.0f * s));
                    CHECK(approxEqual(tl.h, 20.0f * s));

                    const PixelRect c = resolveAt(centred, viewport, s);
                    CHECK(approxEqual(c.x + c.w * 0.5f, p.x + p.w * 0.5f));
                    CHECK(approxEqual(c.y + c.h * 0.5f, p.y + p.h * 0.5f));
                    CHECK(approxEqual(c.w, 80.0f * s));
                    CHECK(approxEqual(c.h, 24.0f * s));

                    const PixelRect f = resolveAt(filled, viewport, s);
                    CHECK(approxEqual(f.x, p.x + 4.0f * s));
                    CHECK(approxEqual(f.y, p.y + 4.0f * s));
                    CHECK(approxEqual(f.w, p.w - 8.0f * s));
                    CHECK(approxEqual(f.h, p.h - 8.0f * s));

                    // The offset moves the child by (12, 8) reference px, +y DOWN.
                    const PixelRect n = resolveAt(nudged, viewport, s);
                    CHECK(approxEqual(n.x, tl.x + 12.0f * s));
                    CHECK(approxEqual(n.y, tl.y + 8.0f * s));
                }
            }
        }
    }

    TEST_CASE("translateRect moves a child by its parent's centre delta without resizing it")
    {
        const std::array<RectLayout, 4> parents = sampleParents();
        for (std::size_t from = 0; from < parents.size(); ++from)
        {
            for (std::size_t to = 0; to < parents.size(); ++to)
            {
                const AffineRect before = utilities::ui::affineRectOf(parents[from]);
                const AffineRect after = utilities::ui::affineRectOf(parents[to]);
                const utilities::ui::AffineDelta delta = utilities::ui::centreDelta(before, after);

                const RectLayout child = utilities::ui::placeInside(
                    before, makePlacement(HAlign::Left, VAlign::Top, {40.0f, 20.0f}, {5.0f, 0.0f, 6.0f, 0.0f}));
                const RectLayout moved = utilities::ui::translateRect(child, delta);

                for (const Viewport& viewport : kViewports)
                {
                    for (bool scaleWithScreen : {true, false})
                    {
                        const float s = canvasScale(viewport, scaleWithScreen);
                        const PixelRect pOld = resolveAt(parents[from], viewport, s);
                        const PixelRect pNew = resolveAt(parents[to], viewport, s);
                        const PixelRect cOld = resolveAt(child, viewport, s);
                        const PixelRect cNew = resolveAt(moved, viewport, s);

                        const float parentDx = (pNew.x + pNew.w * 0.5f) - (pOld.x + pOld.w * 0.5f);
                        const float parentDy = (pNew.y + pNew.h * 0.5f) - (pOld.y + pOld.h * 0.5f);
                        CHECK(approxEqual(cNew.x - cOld.x, parentDx));
                        CHECK(approxEqual(cNew.y - cOld.y, parentDy));
                        CHECK(approxEqual(cNew.w, cOld.w));
                        CHECK(approxEqual(cNew.h, cOld.h));
                    }
                }
            }
        }
    }

    TEST_CASE("HUD worked example (plan 4.4): health bar, score label, pause button")
    {
        const AffineRect canvas = utilities::ui::fullViewportRect();
        const Viewport reference{kRefW, kRefH};

        SUBCASE("HealthBar: topLeft, 320 x 28, margin 24")
        {
            const RectLayout bar = utilities::ui::placeInside(
                canvas, makePlacement(HAlign::Left, VAlign::Top, {320.0f, 28.0f}, glm::vec4(24.0f)));
            CHECK(bar.anchorMin == glm::vec2(0.0f, 1.0f));
            CHECK(bar.anchorMax == glm::vec2(0.0f, 1.0f));
            CHECK(bar.pivot == glm::vec2(0.5f, 0.5f));
            CHECK(bar.sizeDelta == glm::vec2(320.0f, 28.0f));
            CHECK(bar.anchoredPosition == glm::vec2(184.0f, -38.0f));
            checkRectExact(resolveAt(bar, reference, 1.0f), PixelRect{24.0f, 24.0f, 320.0f, 28.0f});

            // 1280 x 720: scale 2/3, the margins scale with the canvas.
            const Viewport hd{1280.0f, 720.0f};
            checkRectNear(resolveAt(bar, hd, canvasScale(hd, true)), PixelRect{16.0f, 16.0f, 213.3333f, 18.6667f});

            // 2560 x 1080: scale min(1.333, 1) = 1, so the bar keeps its reference place.
            const Viewport wide{2560.0f, 1080.0f};
            CHECK(canvasScale(wide, true) == 1.0f);
            checkRectNear(resolveAt(bar, wide, 1.0f), PixelRect{24.0f, 24.0f, 320.0f, 28.0f});

            // A label stretched inside the bar with margin 4 shares its anchor and stays inset.
            const RectLayout label = utilities::ui::placeInside(
                utilities::ui::affineRectOf(bar),
                makePlacement(HAlign::Stretch, VAlign::Stretch, {0.0f, 0.0f}, glm::vec4(4.0f)));
            CHECK(label.anchorMin == glm::vec2(0.0f, 1.0f));
            CHECK(label.anchorMax == glm::vec2(0.0f, 1.0f));
            CHECK(label.sizeDelta == glm::vec2(312.0f, 20.0f));
            CHECK(label.anchoredPosition == glm::vec2(184.0f, -38.0f));
        }

        SUBCASE("ScoreLabel: topRight, 300 x 48, margin [24, 20]")
        {
            const RectLayout score = utilities::ui::placeInside(
                canvas, makePlacement(HAlign::Right, VAlign::Top, {300.0f, 48.0f}, {24.0f, 24.0f, 20.0f, 20.0f}));
            CHECK(score.anchorMin == glm::vec2(1.0f, 1.0f));
            CHECK(score.anchorMax == glm::vec2(1.0f, 1.0f));
            CHECK(score.sizeDelta == glm::vec2(300.0f, 48.0f));
            CHECK(score.anchoredPosition == glm::vec2(-174.0f, -44.0f));
            checkRectExact(resolveAt(score, reference, 1.0f), PixelRect{1596.0f, 20.0f, 300.0f, 48.0f});
        }

        SUBCASE("PauseButton: top, 160 x 56, margin [0, 20]")
        {
            const RectLayout pause = utilities::ui::placeInside(
                canvas, makePlacement(HAlign::Center, VAlign::Top, {160.0f, 56.0f}, {0.0f, 0.0f, 20.0f, 20.0f}));
            CHECK(pause.anchorMin == glm::vec2(0.5f, 1.0f));
            CHECK(pause.anchorMax == glm::vec2(0.5f, 1.0f));
            CHECK(pause.sizeDelta == glm::vec2(160.0f, 56.0f));
            CHECK(pause.anchoredPosition == glm::vec2(0.0f, -48.0f));
            checkRectExact(resolveAt(pause, reference, 1.0f), PixelRect{880.0f, 20.0f, 160.0f, 56.0f});
        }
    }

    TEST_CASE("solveRectKeepingAnchors inverts resolveRect at the reference resolution")
    {
        Lcg rng;
        for (int i = 0; i < 200; ++i)
        {
            const RectLayout layout = randomLayout(rng);
            const PixelRect target{rng.range(-100.0f, 1800.0f), rng.range(-100.0f, 1000.0f),
                                   rng.range(1.0f, 600.0f), rng.range(1.0f, 400.0f)};

            const RectLayout solved = utilities::ui::solveRectKeepingAnchors(layout, target, kRefW, kRefH);
            CHECK(solved.anchorMin == layout.anchorMin);
            CHECK(solved.anchorMax == layout.anchorMax);
            CHECK(solved.pivot == layout.pivot);
            checkRectNear(utilities::ui::resolveRect(solved, kRefW, kRefH, 1.0f), target);
        }
    }

    TEST_CASE("canvasRectYUp is the reference-size rect in the canvas' y-up frame")
    {
        Lcg rng;
        for (int i = 0; i < 100; ++i)
        {
            const RectLayout layout = randomLayout(rng);
            const PixelRect p = utilities::ui::resolveRect(layout, kRefW, kRefH, 1.0f);
            const utilities::ui::CanvasRectYUp up = utilities::ui::canvasRectYUp(layout, kRefW, kRefH);
            CHECK(up.left == p.x);
            CHECK(up.bottom == kRefH - (p.y + p.h));
            CHECK(up.w == p.w);
            CHECK(up.h == p.h);
        }
    }
}
