#include <doctest.h>
#include <glm/glm.hpp>
#include <cmath>
#include <iterator>

// VK-1435 — UI Layer Builder on-canvas handle math.
//
// UILayerCanvasHandles is an editor-only TU (under VFEngine/editor, not compiled by the
// Tests project). It has no ImGui / editor-runtime dependencies — only services::UIRectData
// (DTOs) + glm — so we compile its source directly into this test TU (the "VFEngine/editor"
// includedir makes the path resolve). This exercises the REAL inversion code, not a mirror.
#include "windows/uilayer/UILayerCanvasHandles.cpp"

// The forward layout math the inverse must agree with (the runtime UI resolver).
#include <ui/UIRectMath.hpp>
#include <components/Components.hpp>

using namespace windows::uilayer;

namespace
{
    constexpr float kCanvasW = 1920.0f;
    constexpr float kCanvasH = 1080.0f;

    // Build a UIRectData with explicit fields.
    services::UIRectData makeRect(glm::vec2 aMin, glm::vec2 aMax, glm::vec2 pivot,
                                  glm::vec2 sizeDelta, glm::vec2 anchoredPos)
    {
        services::UIRectData r;
        r.anchorMin = aMin;
        r.anchorMax = aMax;
        r.pivot = pivot;
        r.sizeDelta = sizeDelta;
        r.anchoredPosition = anchoredPos;
        return r;
    }

    // Resolve a UIRectData through the SHIPPING runtime math (UIRectMath), so the test is
    // anchored to engine behavior rather than to resolveRefRect (which mirrors it).
    RefRect runtimeResolve(const services::UIRectData& d)
    {
        components::UIRectComponent comp;
        comp.anchorMin = d.anchorMin;
        comp.anchorMax = d.anchorMax;
        comp.pivot = d.pivot;
        comp.sizeDelta = d.sizeDelta;
        comp.anchoredPosition = d.anchoredPosition;
        utilities::ui::PixelRect pr = utilities::ui::resolvePixelRect(comp, kCanvasW, kCanvasH, 1.0f);
        return RefRect{pr.x, pr.y, pr.w, pr.h};
    }

    void checkRectNear(const RefRect& a, const RefRect& b, float eps = 1e-3f)
    {
        CHECK(a.x == doctest::Approx(b.x).epsilon(eps));
        CHECK(a.y == doctest::Approx(b.y).epsilon(eps));
        CHECK(a.w == doctest::Approx(b.w).epsilon(eps));
        CHECK(a.h == doctest::Approx(b.h).epsilon(eps));
    }
}

TEST_SUITE("UILayerBuilderHandles")
{
    TEST_CASE("resolveRefRect matches the runtime UIRectMath resolver")
    {
        // A spread of anchor/pivot/offset configurations.
        services::UIRectData cases[] = {
            makeRect({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {200.0f, 60.0f}, {0.0f, 0.0f}),
            makeRect({0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}, {-40.0f, -40.0f}, {0.0f, 0.0f}),
            makeRect({0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}, {120.0f, 48.0f}, {32.0f, -32.0f}),
            makeRect({1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}, {80.0f, 80.0f}, {-16.0f, 16.0f}),
            makeRect({0.25f, 0.25f}, {0.75f, 0.75f}, {0.3f, 0.7f}, {10.0f, -10.0f}, {5.0f, -7.0f}),
        };

        for (const auto& d : cases)
        {
            RefRect mine = resolveRefRect(d, kCanvasW, kCanvasH);
            RefRect runtime = runtimeResolve(d);
            checkRectNear(mine, runtime);
        }
    }

    TEST_CASE("solveRectData inverts resolveRefRect (round-trip), keeping anchors/pivot")
    {
        // Start from an element, pick an arbitrary target rect, solve, then re-resolve:
        // the resolved rect must equal the target, and anchors/pivot must be untouched.
        services::UIRectData start =
            makeRect({0.2f, 0.3f}, {0.6f, 0.8f}, {0.4f, 0.6f}, {12.0f, -8.0f}, {3.0f, 9.0f});

        RefRect target{640.0f, 360.0f, 300.0f, 150.0f};
        services::UIRectData solved = solveRectData(start, target, kCanvasW, kCanvasH);

        // Anchors + pivot preserved.
        CHECK(solved.anchorMin.x == doctest::Approx(start.anchorMin.x));
        CHECK(solved.anchorMin.y == doctest::Approx(start.anchorMin.y));
        CHECK(solved.anchorMax.x == doctest::Approx(start.anchorMax.x));
        CHECK(solved.anchorMax.y == doctest::Approx(start.anchorMax.y));
        CHECK(solved.pivot.x == doctest::Approx(start.pivot.x));
        CHECK(solved.pivot.y == doctest::Approx(start.pivot.y));

        // Resolved rect equals the target (via the runtime resolver).
        checkRectNear(runtimeResolve(solved), target);
    }

    TEST_CASE("Move drag shifts anchoredPosition, preserving size (with y-flip)")
    {
        services::UIRectData start =
            makeRect({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {200.0f, 60.0f}, {0.0f, 0.0f});
        RefRect startRect = runtimeResolve(start);

        glm::vec2 deltaRef{40.0f, -25.0f}; // move right + up (y down screen space)
        RefRect target = applyHandleDrag(startRect, HandleKind::Body, deltaRef);

        // Move preserves size.
        CHECK(target.w == doctest::Approx(startRect.w));
        CHECK(target.h == doctest::Approx(startRect.h));
        CHECK(target.x == doctest::Approx(startRect.x + 40.0f));
        CHECK(target.y == doctest::Approx(startRect.y - 25.0f));

        services::UIRectData solved = solveRectData(start, target, kCanvasW, kCanvasH);
        // sizeDelta unchanged (size held), anchoredPosition.x = +40, anchoredPosition.y = +25
        // (y-flip: screen-up == anchoredPosition +y).
        CHECK(solved.sizeDelta.x == doctest::Approx(start.sizeDelta.x));
        CHECK(solved.sizeDelta.y == doctest::Approx(start.sizeDelta.y));
        CHECK(solved.anchoredPosition.x == doctest::Approx(40.0f));
        CHECK(solved.anchoredPosition.y == doctest::Approx(25.0f));

        checkRectNear(runtimeResolve(solved), target);
    }

    TEST_CASE("Resize from the right edge grows width, pins the left edge")
    {
        services::UIRectData start =
            makeRect({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {200.0f, 60.0f}, {0.0f, 0.0f});
        RefRect startRect = runtimeResolve(start);

        RefRect target = applyHandleDrag(startRect, HandleKind::Right, glm::vec2(50.0f, 0.0f));
        CHECK(target.x == doctest::Approx(startRect.x));            // left pinned
        CHECK(target.w == doctest::Approx(startRect.w + 50.0f));    // width grew
        CHECK(target.h == doctest::Approx(startRect.h));

        services::UIRectData solved = solveRectData(start, target, kCanvasW, kCanvasH);
        checkRectNear(runtimeResolve(solved), target);
        CHECK(solved.sizeDelta.x == doctest::Approx(start.sizeDelta.x + 50.0f));
    }

    TEST_CASE("Resize from the top-left corner pins the bottom-right")
    {
        services::UIRectData start =
            makeRect({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {200.0f, 100.0f}, {0.0f, 0.0f});
        RefRect startRect = runtimeResolve(start);
        float br_x = startRect.right();
        float br_y = startRect.bottom();

        RefRect target = applyHandleDrag(startRect, HandleKind::TopLeft, glm::vec2(-30.0f, -20.0f));
        // Bottom-right corner stays fixed.
        CHECK(target.right() == doctest::Approx(br_x));
        CHECK(target.bottom() == doctest::Approx(br_y));
        CHECK(target.w == doctest::Approx(startRect.w + 30.0f));
        CHECK(target.h == doctest::Approx(startRect.h + 20.0f));

        services::UIRectData solved = solveRectData(start, target, kCanvasW, kCanvasH);
        checkRectNear(runtimeResolve(solved), target);
    }

    TEST_CASE("Resize enforces a minimum size and never inverts the rect")
    {
        services::UIRectData start =
            makeRect({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {100.0f, 40.0f}, {0.0f, 0.0f});
        RefRect startRect = runtimeResolve(start);

        // Drag the right edge far past the left edge.
        RefRect target = applyHandleDrag(startRect, HandleKind::Right, glm::vec2(-1000.0f, 0.0f), 4.0f);
        CHECK(target.w >= 4.0f);
        CHECK(target.x == doctest::Approx(startRect.x)); // left still pinned
    }

    TEST_CASE("reanchorKeepingVisual keeps the element visually fixed")
    {
        // Element anchored bottom-left, re-anchored to stretch full-width: the resolved
        // rect must not move.
        services::UIRectData start =
            makeRect({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {300.0f, 120.0f}, {50.0f, 40.0f});
        RefRect before = runtimeResolve(start);

        services::UIRectData re =
            reanchorKeepingVisual(start, glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), kCanvasW, kCanvasH);

        // New anchors applied.
        CHECK(re.anchorMin.x == doctest::Approx(0.0f));
        CHECK(re.anchorMax.x == doctest::Approx(1.0f));
        // Visual rect unchanged.
        checkRectNear(runtimeResolve(re), before);
    }

    TEST_CASE("hitTestHandle prioritizes corners, then edges, then body")
    {
        RefRect rect{100.0f, 100.0f, 200.0f, 100.0f};
        const float grab = 6.0f;

        // Exactly on the top-left corner.
        CHECK(hitTestHandle(rect, glm::vec2(100.0f, 100.0f), grab) == HandleKind::TopLeft);
        // Mid top edge.
        CHECK(hitTestHandle(rect, glm::vec2(200.0f, 100.0f), grab) == HandleKind::Top);
        // Right edge mid.
        CHECK(hitTestHandle(rect, glm::vec2(300.0f, 150.0f), grab) == HandleKind::Right);
        // Interior -> body.
        CHECK(hitTestHandle(rect, glm::vec2(200.0f, 150.0f), grab) == HandleKind::Body);
        // Outside -> none.
        CHECK(hitTestHandle(rect, glm::vec2(500.0f, 500.0f), grab) == HandleKind::None);
    }

    TEST_CASE("Letterbox mapping round-trips ref<->screen and centers")
    {
        glm::vec2 regionOrigin{10.0f, 20.0f};
        glm::vec2 regionSize{960.0f, 540.0f}; // exactly half of 1920x1080 -> scale 0.5, no letterbox bars
        LetterboxMapping m = makeLetterbox(regionOrigin, regionSize, glm::vec2(1920.0f, 1080.0f));

        CHECK(m.scale == doctest::Approx(0.5f));
        // ref (0,0) maps to the image top-left; round-trip back.
        glm::vec2 s = m.refToScreen(glm::vec2(0.0f, 0.0f));
        glm::vec2 r = m.screenToRef(s);
        CHECK(r.x == doctest::Approx(0.0f));
        CHECK(r.y == doctest::Approx(0.0f));
        // ref center maps to region center.
        glm::vec2 center = m.refToScreen(glm::vec2(960.0f, 540.0f));
        CHECK(center.x == doctest::Approx(regionOrigin.x + regionSize.x * 0.5f));
        CHECK(center.y == doctest::Approx(regionOrigin.y + regionSize.y * 0.5f));
    }

    // -------------------------------------------------------------------------
    // VK-1435 — depth coverage beyond the 9 baseline cases. These hammer the
    // non-trivial anchor/pivot/sizeDelta configurations (stretch anchors,
    // off-center pivots, negative anchoredPosition) that the simple centered
    // cases never exercise, and stress the solve→resolve round-trip across the
    // full set of handle gestures.
    // -------------------------------------------------------------------------

    TEST_CASE("resolveRefRect matches the runtime resolver for stretch / off-center / "
              "negative-offset configurations")
    {
        services::UIRectData cases[] = {
            // Horizontal stretch (anchorMin.x=0, anchorMax.x=1), bottom-pinned, top-left pivot.
            makeRect({0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}, {-64.0f, 90.0f}, {0.0f, 24.0f}),
            // Vertical stretch, right-anchored, bottom-right pivot.
            makeRect({1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f}, {-30.0f, -120.0f}, {-12.0f, 0.0f}),
            // Full stretch with an inset (classic full-screen panel inset by margins).
            makeRect({0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}, {-200.0f, -200.0f}, {0.0f, 0.0f}),
            // Off-center pivot + negative anchoredPosition in both axes.
            makeRect({0.5f, 0.5f}, {0.5f, 0.5f}, {0.15f, 0.85f}, {256.0f, 128.0f}, {-77.0f, -33.0f}),
            // Asymmetric partial anchors with a fractional pivot.
            makeRect({0.1f, 0.6f}, {0.9f, 0.95f}, {0.25f, 0.4f}, {-18.0f, 22.0f}, {13.0f, -9.0f}),
        };

        for (const auto& d : cases)
        {
            RefRect mine = resolveRefRect(d, kCanvasW, kCanvasH);
            RefRect runtime = runtimeResolve(d);
            checkRectNear(mine, runtime);
        }
    }

    TEST_CASE("solveRectData round-trips for every handle gesture from a stretch-anchored "
              "off-center-pivot element")
    {
        // A genuinely awkward starting element: horizontal stretch, vertical centered,
        // pivot way off-center, non-zero anchoredPosition. apply→solve→resolve must hit
        // the target rect for move, each edge, each corner, and re-anchor.
        services::UIRectData start =
            makeRect({0.0f, 0.5f}, {1.0f, 0.5f}, {0.2f, 0.8f}, {-50.0f, 70.0f}, {15.0f, -22.0f});
        RefRect startRect = runtimeResolve(start);

        const HandleKind gestures[] = {
            HandleKind::Body, HandleKind::Left, HandleKind::Right, HandleKind::Top,
            HandleKind::Bottom, HandleKind::TopLeft, HandleKind::TopRight,
            HandleKind::BottomLeft, HandleKind::BottomRight
        };
        const glm::vec2 deltas[] = {
            {40.0f, -25.0f}, {-30.0f, 0.0f}, {50.0f, 0.0f}, {0.0f, -18.0f},
            {0.0f, 27.0f}, {-22.0f, -14.0f}, {19.0f, -11.0f}, {-16.0f, 21.0f}, {33.0f, 17.0f}
        };

        for (size_t i = 0; i < std::size(gestures); ++i)
        {
            RefRect target = applyHandleDrag(startRect, gestures[i], deltas[i]);
            services::UIRectData solved = solveRectData(start, target, kCanvasW, kCanvasH);

            // Anchors + pivot are never touched by a solve.
            CHECK(solved.anchorMin.x == doctest::Approx(start.anchorMin.x));
            CHECK(solved.anchorMin.y == doctest::Approx(start.anchorMin.y));
            CHECK(solved.anchorMax.x == doctest::Approx(start.anchorMax.x));
            CHECK(solved.anchorMax.y == doctest::Approx(start.anchorMax.y));
            CHECK(solved.pivot.x == doctest::Approx(start.pivot.x));
            CHECK(solved.pivot.y == doctest::Approx(start.pivot.y));

            // The resolved rect of the solved data equals the target (via the runtime resolver).
            checkRectNear(runtimeResolve(solved), target);
        }
    }

    TEST_CASE("Move drag leaves sizeDelta untouched across off-center pivot + stretch anchors")
    {
        // Moving must change ONLY anchoredPosition — sizeDelta is invariant regardless of
        // pivot/anchor config, because the rect's size doesn't change.
        services::UIRectData start =
            makeRect({0.0f, 0.0f}, {1.0f, 1.0f}, {0.1f, 0.9f}, {-120.0f, -80.0f}, {40.0f, -15.0f});
        RefRect startRect = runtimeResolve(start);

        RefRect target = applyHandleDrag(startRect, HandleKind::Body, glm::vec2(-37.0f, 52.0f));
        services::UIRectData solved = solveRectData(start, target, kCanvasW, kCanvasH);

        CHECK(solved.sizeDelta.x == doctest::Approx(start.sizeDelta.x));
        CHECK(solved.sizeDelta.y == doctest::Approx(start.sizeDelta.y));
        // x follows the screen-space delta directly; y flips (screen-down == anchoredPosition -y).
        CHECK(solved.anchoredPosition.x == doctest::Approx(start.anchoredPosition.x - 37.0f));
        CHECK(solved.anchoredPosition.y == doctest::Approx(start.anchoredPosition.y - 52.0f));
        checkRectNear(runtimeResolve(solved), target);
    }

    TEST_CASE("Min-size clamp never inverts the rect for any single resize handle")
    {
        // Drag each resize handle far past its opposite edge; the result must stay a
        // valid (non-inverted, >= minSize) rect with the opposite edge pinned.
        services::UIRectData start =
            makeRect({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {120.0f, 80.0f}, {0.0f, 0.0f});
        RefRect s = runtimeResolve(start);
        const float minSize = 5.0f;

        struct Case { HandleKind k; glm::vec2 d; };
        const Case cases[] = {
            {HandleKind::Left,  { 1000.0f, 0.0f}},  // left edge dragged right past the right edge
            {HandleKind::Right, {-1000.0f, 0.0f}},  // right edge dragged left past the left edge
            {HandleKind::Top,   {0.0f,  1000.0f}},  // top dragged below the bottom
            {HandleKind::Bottom,{0.0f, -1000.0f}},  // bottom dragged above the top
            {HandleKind::TopLeft, { 1000.0f, 1000.0f}},
            {HandleKind::BottomRight, {-1000.0f, -1000.0f}},
        };

        for (const auto& c : cases)
        {
            RefRect t = applyHandleDrag(s, c.k, c.d, minSize);
            CHECK(t.w >= minSize);
            CHECK(t.h >= minSize);

            // Opposite edge stays pinned (within the unmoved axis).
            switch (c.k)
            {
            case HandleKind::Left:        CHECK(t.right()  == doctest::Approx(s.right()));  break;
            case HandleKind::Right:       CHECK(t.x        == doctest::Approx(s.x));         break;
            case HandleKind::Top:         CHECK(t.bottom() == doctest::Approx(s.bottom()));  break;
            case HandleKind::Bottom:      CHECK(t.y        == doctest::Approx(s.y));         break;
            case HandleKind::TopLeft:     CHECK(t.right()  == doctest::Approx(s.right()));
                                          CHECK(t.bottom() == doctest::Approx(s.bottom()));  break;
            case HandleKind::BottomRight: CHECK(t.x        == doctest::Approx(s.x));
                                          CHECK(t.y        == doctest::Approx(s.y));         break;
            default: break;
            }
        }
    }

    TEST_CASE("reanchorKeepingVisual is visually fixed across several anchor migrations")
    {
        // Re-anchoring from any source config to any target anchor pair must keep the
        // resolved rect byte-identical — this is the load-bearing back-solve.
        services::UIRectData starts[] = {
            makeRect({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {300.0f, 120.0f}, {50.0f, 40.0f}),
            makeRect({1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f}, {160.0f, 90.0f}, {-24.0f, -24.0f}),
            makeRect({0.5f, 0.5f}, {0.5f, 0.5f}, {0.3f, 0.7f}, {220.0f, 140.0f}, {18.0f, -9.0f}),
        };
        // Target anchor pairs: corner-pin, full-stretch, horizontal-stretch, vertical-stretch.
        struct AnchorPair { glm::vec2 min; glm::vec2 max; };
        const AnchorPair targets[] = {
            {{0.0f, 1.0f}, {0.0f, 1.0f}}, // top-left corner pin
            {{0.0f, 0.0f}, {1.0f, 1.0f}}, // full stretch
            {{0.0f, 0.3f}, {1.0f, 0.3f}}, // horizontal stretch at a non-trivial y
            {{0.6f, 0.0f}, {0.6f, 1.0f}}, // vertical stretch at a non-trivial x
        };

        for (const auto& start : starts)
        {
            RefRect before = runtimeResolve(start);
            for (const auto& tgt : targets)
            {
                services::UIRectData re =
                    reanchorKeepingVisual(start, tgt.min, tgt.max, kCanvasW, kCanvasH);
                CHECK(re.anchorMin.x == doctest::Approx(tgt.min.x));
                CHECK(re.anchorMin.y == doctest::Approx(tgt.min.y));
                CHECK(re.anchorMax.x == doctest::Approx(tgt.max.x));
                CHECK(re.anchorMax.y == doctest::Approx(tgt.max.y));
                // Pivot preserved (re-anchor never moves the pivot).
                CHECK(re.pivot.x == doctest::Approx(start.pivot.x));
                CHECK(re.pivot.y == doctest::Approx(start.pivot.y));
                // Visually fixed.
                checkRectNear(runtimeResolve(re), before);
            }
        }
    }

    TEST_CASE("Letterbox mapping round-trips an arbitrary point under zoom + pan")
    {
        // A non-unit zoom and a non-zero pan must still round-trip screen<->ref exactly.
        glm::vec2 regionOrigin{120.0f, 64.0f};
        glm::vec2 regionSize{800.0f, 600.0f};
        glm::vec2 refExtent{1280.0f, 720.0f};
        LetterboxMapping m = makeLetterbox(regionOrigin, regionSize, refExtent, 1.75f,
                                           glm::vec2(40.0f, -25.0f));

        for (glm::vec2 ref : {glm::vec2(0.0f, 0.0f), glm::vec2(640.0f, 360.0f),
                              glm::vec2(1280.0f, 720.0f), glm::vec2(-100.0f, 1000.0f)})
        {
            glm::vec2 screen = m.refToScreen(ref);
            glm::vec2 back = m.screenToRef(screen);
            CHECK(back.x == doctest::Approx(ref.x));
            CHECK(back.y == doctest::Approx(ref.y));
        }
    }
}
