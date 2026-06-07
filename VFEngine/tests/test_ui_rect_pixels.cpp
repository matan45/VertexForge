#include <doctest.h>
#include <components/Components.hpp>
#include <ui/UIRectMath.hpp>
#include <glm/glm.hpp>

// VK-1302: UI::setRectPixels positions a UIRect in viewport pixels by writing
// pure NORMALIZED anchors (sizeDelta/anchoredPosition zeroed). This mirrors
// UIComponentService::setUIRectPixels (UICanvasRectImageOps.cpp) and asserts:
//  (a) resolvePixelRect lands exactly on the requested rect in the same space,
//  (b) the rect stays proportionally correct when the UI pass resolves against
//      a DIFFERENT extent than the mouse/viewport space (editor play panel vs
//      swapchain render extent) — the bug behind the drag-box cursor offset.

namespace
{
    // Same math as UIComponentService::setUIRectPixels.
    void applyRectPixels(components::UIRectComponent& comp,
                         float x, float y, float w, float h,
                         float vw, float vh)
    {
        comp.anchorMin = glm::vec2(x / vw, 1.0f - (y + h) / vh);
        comp.anchorMax = glm::vec2((x + w) / vw, 1.0f - y / vh);
        comp.pivot = glm::vec2(0.5f, 0.5f);
        comp.sizeDelta = glm::vec2(0.0f, 0.0f);
        comp.anchoredPosition = glm::vec2(0.0f, 0.0f);
    }
}

TEST_SUITE("UIRectPixels")
{
    TEST_CASE("resolves to the exact requested rect in the same extent")
    {
        components::UIRectComponent comp;
        applyRectPixels(comp, 120.0f, 80.0f, 300.0f, 200.0f, 1920.0f, 1080.0f);

        // Canvas scale must not matter for pure-anchor rects: check several.
        for (float scale : {1.0f, 0.6667f, 1.5f})
        {
            utilities::ui::PixelRect rect =
                utilities::ui::resolvePixelRect(comp, 1920.0f, 1080.0f, scale);
            CHECK(rect.x == doctest::Approx(120.0f));
            CHECK(rect.y == doctest::Approx(80.0f));
            CHECK(rect.w == doctest::Approx(300.0f));
            CHECK(rect.h == doctest::Approx(200.0f));
        }
    }

    TEST_CASE("rect is extent-invariant: render extent != mouse viewport")
    {
        components::UIRectComponent comp;
        // Mouse space: a 1280x720 play panel; drag rect at (200, 150) size 320x180.
        applyRectPixels(comp, 200.0f, 150.0f, 320.0f, 180.0f, 1280.0f, 720.0f);

        // UI pass resolves against a larger swapchain extent (2560x1440).
        utilities::ui::PixelRect rect =
            utilities::ui::resolvePixelRect(comp, 2560.0f, 1440.0f, 1.3333f);

        // Same normalized rect: 2x panel coordinates in a 2x extent.
        CHECK(rect.x == doctest::Approx(400.0f));
        CHECK(rect.y == doctest::Approx(300.0f));
        CHECK(rect.w == doctest::Approx(640.0f));
        CHECK(rect.h == doctest::Approx(360.0f));
    }

    TEST_CASE("zero-size rect collapses cleanly (drag start frame)")
    {
        components::UIRectComponent comp;
        applyRectPixels(comp, 640.0f, 360.0f, 0.0f, 0.0f, 1920.0f, 1080.0f);

        utilities::ui::PixelRect rect =
            utilities::ui::resolvePixelRect(comp, 1920.0f, 1080.0f, 1.0f);
        CHECK(rect.x == doctest::Approx(640.0f));
        CHECK(rect.y == doctest::Approx(360.0f));
        CHECK(rect.w == doctest::Approx(0.0f));
        CHECK(rect.h == doctest::Approx(0.0f));
    }

    TEST_CASE("y axis maps top-down: y=0 is the top edge")
    {
        components::UIRectComponent comp;
        applyRectPixels(comp, 0.0f, 0.0f, 100.0f, 50.0f, 1920.0f, 1080.0f);

        utilities::ui::PixelRect rect =
            utilities::ui::resolvePixelRect(comp, 1920.0f, 1080.0f, 1.0f);
        CHECK(rect.x == doctest::Approx(0.0f));
        CHECK(rect.y == doctest::Approx(0.0f)); // top-left corner of the screen
        CHECK(rect.w == doctest::Approx(100.0f));
        CHECK(rect.h == doctest::Approx(50.0f));
    }

    // VK-1315: UI::getRectPixels reads an AUTHORED rect back in viewport pixels
    // via the same resolvePixelRect path. Layout mirrors the RTS minimap view:
    // bottom-left anchors (0,0), pivot (0,1), anchoredPosition (40,12),
    // sizeDelta (210,197) under a ScaleWithScreenSize canvas (ref 1920x1080).
    TEST_CASE("authored bottom-left rect resolves to viewport pixels (getRectPixels)")
    {
        components::UIRectComponent comp;
        comp.anchorMin = glm::vec2(0.0f, 0.0f);
        comp.anchorMax = glm::vec2(0.0f, 0.0f);
        comp.pivot = glm::vec2(0.0f, 1.0f);
        comp.anchoredPosition = glm::vec2(40.0f, 12.0f);
        comp.sizeDelta = glm::vec2(210.0f, 197.0f);

        // Native viewport == canvas reference: scale 1.
        utilities::ui::PixelRect rect =
            utilities::ui::resolvePixelRect(comp, 1920.0f, 1080.0f, 1.0f);
        CHECK(rect.x == doctest::Approx(40.0f));
        CHECK(rect.y == doctest::Approx(1080.0f - 12.0f - 197.0f));
        CHECK(rect.w == doctest::Approx(210.0f));
        CHECK(rect.h == doctest::Approx(197.0f));

        // Half-size viewport: ScaleWithScreenSize gives scale 0.5, so offsets
        // and size shrink with it while staying glued to the bottom-left.
        rect = utilities::ui::resolvePixelRect(comp, 960.0f, 540.0f, 0.5f);
        CHECK(rect.x == doctest::Approx(20.0f));
        CHECK(rect.y == doctest::Approx(540.0f - 6.0f - 98.5f));
        CHECK(rect.w == doctest::Approx(105.0f));
        CHECK(rect.h == doctest::Approx(98.5f));
    }
}
