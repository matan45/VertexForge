#include <doctest.h>
#include <components/Components.hpp>
#include <ui/UIRectMath.hpp>
#include <glm/glm.hpp>

// VK-1302: UI::setRectPixels positions a UIRect in viewport pixels by inverting
// resolvePixelRect (UIRectMath.hpp). This mirrors the inversion implemented in
// UIComponentService::setUIRectPixels and asserts the round-trip lands exactly
// on the requested viewport rect for various anchor/pivot/scale setups.

namespace
{
    // Same math as UIComponentService::setUIRectPixels (UICanvasRectImageOps.cpp).
    void applyRectPixels(components::UIRectComponent& comp,
                         float x, float y, float w, float h,
                         float vw, float vh, float scale)
    {
        float anchorLeftPx  = comp.anchorMin.x * vw;
        float anchorRightPx = comp.anchorMax.x * vw;
        float anchorTopPx   = (1.0f - comp.anchorMax.y) * vh;
        float anchorBotPx   = (1.0f - comp.anchorMin.y) * vh;

        comp.sizeDelta.x = (w - (anchorRightPx - anchorLeftPx)) / scale;
        comp.sizeDelta.y = (h - (anchorBotPx - anchorTopPx)) / scale;

        float cx = x + comp.pivot.x * w;
        float cy = y + comp.pivot.y * h;
        comp.anchoredPosition.x = (cx - (anchorLeftPx + anchorRightPx) * 0.5f) / scale;
        comp.anchoredPosition.y = ((anchorTopPx + anchorBotPx) * 0.5f - cy) / scale;
    }

    void checkRoundTrip(const components::UIRectComponent& comp,
                        float x, float y, float w, float h,
                        float vw, float vh, float scale)
    {
        utilities::ui::PixelRect rect = utilities::ui::resolvePixelRect(comp, vw, vh, scale);
        CHECK(rect.x == doctest::Approx(x));
        CHECK(rect.y == doctest::Approx(y));
        CHECK(rect.w == doctest::Approx(w));
        CHECK(rect.h == doctest::Approx(h));
    }
}

TEST_SUITE("UIRectPixels")
{
    TEST_CASE("top-left anchored rect (drag-box authoring) round-trips at scale 1")
    {
        components::UIRectComponent comp;
        comp.anchorMin = {0.0f, 1.0f};
        comp.anchorMax = {0.0f, 1.0f};
        comp.pivot = {0.0f, 0.0f};

        applyRectPixels(comp, 120.0f, 80.0f, 300.0f, 200.0f, 1920.0f, 1080.0f, 1.0f);
        checkRoundTrip(comp, 120.0f, 80.0f, 300.0f, 200.0f, 1920.0f, 1080.0f, 1.0f);
    }

    TEST_CASE("round-trips under a non-unit canvas scale")
    {
        components::UIRectComponent comp;
        comp.anchorMin = {0.0f, 1.0f};
        comp.anchorMax = {0.0f, 1.0f};
        comp.pivot = {0.0f, 0.0f};

        // e.g. 1280x720 viewport against a 1920x1080 reference canvas
        float scale = 1280.0f / 1920.0f;
        applyRectPixels(comp, 50.0f, 40.0f, 250.0f, 125.0f, 1280.0f, 720.0f, scale);
        checkRoundTrip(comp, 50.0f, 40.0f, 250.0f, 125.0f, 1280.0f, 720.0f, scale);
    }

    TEST_CASE("round-trips regardless of authored anchors and pivot")
    {
        components::UIRectComponent comp;
        comp.anchorMin = {0.5f, 0.5f};
        comp.anchorMax = {0.5f, 0.5f};
        comp.pivot = {0.5f, 0.5f};

        applyRectPixels(comp, 400.0f, 300.0f, 160.0f, 90.0f, 1920.0f, 1080.0f, 1.0f);
        checkRoundTrip(comp, 400.0f, 300.0f, 160.0f, 90.0f, 1920.0f, 1080.0f, 1.0f);

        // Stretched anchors: sizeDelta turns into a delta against the anchor box.
        comp.anchorMin = {0.25f, 0.0f};
        comp.anchorMax = {0.75f, 1.0f};
        comp.pivot = {0.0f, 1.0f};

        applyRectPixels(comp, 10.0f, 20.0f, 600.0f, 400.0f, 1920.0f, 1080.0f, 0.75f);
        checkRoundTrip(comp, 10.0f, 20.0f, 600.0f, 400.0f, 1920.0f, 1080.0f, 0.75f);
    }

    TEST_CASE("zero-size rect collapses cleanly (drag start frame)")
    {
        components::UIRectComponent comp;
        comp.anchorMin = {0.0f, 1.0f};
        comp.anchorMax = {0.0f, 1.0f};
        comp.pivot = {0.0f, 0.0f};

        applyRectPixels(comp, 640.0f, 360.0f, 0.0f, 0.0f, 1920.0f, 1080.0f, 1.0f);
        checkRoundTrip(comp, 640.0f, 360.0f, 0.0f, 0.0f, 1920.0f, 1080.0f, 1.0f);
    }
}
