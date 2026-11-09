#include <doctest.h>
#include <components/Components.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <ui/UIRectMath.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cmath>
#include <limits>

// Editor viewport UI picking: in edit mode UI elements render as world-space
// quads (computeCanvasImageModelMatrix maps the unit quad onto the canvas at
// the canvas entity's world transform). Picking ray-tests those quads.

namespace
{
    components::UICanvasComponent makeCanvas(float w = 1920.0f, float h = 1080.0f, float ppu = 100.0f)
    {
        components::UICanvasComponent canvas;
        canvas.referenceWidth = w;
        canvas.referenceHeight = h;
        canvas.pixelsPerUnit = ppu;
        return canvas;
    }
}

TEST_SUITE("UIPickMath")
{
    TEST_CASE("full-stretch rect maps to the whole canvas in world space")
    {
        auto canvas = makeCanvas(); // 19.2 x 10.8 world units
        components::UIRectComponent rect;
        rect.anchorMin = {0.0f, 0.0f};
        rect.anchorMax = {1.0f, 1.0f};
        rect.sizeDelta = {0.0f, 0.0f};
        rect.anchoredPosition = {0.0f, 0.0f};

        glm::mat4 model = utilities::ui::computeCanvasImageModelMatrix(canvas, glm::mat4(1.0f), rect);
        glm::vec3 corners[4];
        utilities::ui::computeWorldQuadCorners(model, corners);

        // TL
        CHECK(corners[0].x == doctest::Approx(-9.6f));
        CHECK(corners[0].y == doctest::Approx(5.4f));
        // BR
        CHECK(corners[2].x == doctest::Approx(9.6f));
        CHECK(corners[2].y == doctest::Approx(-5.4f));
        // Slight z offset above the canvas plane
        CHECK(corners[0].z == doctest::Approx(0.001f));
    }

    TEST_CASE("centered fixed-size element maps to sizeDelta/pixelsPerUnit world units")
    {
        auto canvas = makeCanvas();
        components::UIRectComponent rect;
        rect.anchorMin = {0.5f, 0.5f};
        rect.anchorMax = {0.5f, 0.5f};
        rect.sizeDelta = {200.0f, 100.0f}; // 2.0 x 1.0 world units
        rect.anchoredPosition = {0.0f, 0.0f};

        glm::mat4 model = utilities::ui::computeCanvasImageModelMatrix(canvas, glm::mat4(1.0f), rect);
        glm::vec3 corners[4];
        utilities::ui::computeWorldQuadCorners(model, corners);

        CHECK(corners[0].x == doctest::Approx(-1.0f));
        CHECK(corners[0].y == doctest::Approx(0.5f));
        CHECK(corners[2].x == doctest::Approx(1.0f));
        CHECK(corners[2].y == doctest::Approx(-0.5f));
    }

    TEST_CASE("ray-quad intersection: hit, miss, behind, parallel")
    {
        auto canvas = makeCanvas();
        components::UIRectComponent rect; // full stretch by default? set explicitly
        rect.anchorMin = {0.0f, 0.0f};
        rect.anchorMax = {1.0f, 1.0f};

        glm::mat4 model = utilities::ui::computeCanvasImageModelMatrix(canvas, glm::mat4(1.0f), rect);
        glm::vec3 corners[4];
        utilities::ui::computeWorldQuadCorners(model, corners);

        SUBCASE("ray through the center hits at the plane distance")
        {
            auto t = utilities::ui::intersectRayQuad(
                {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}, corners);
            REQUIRE(t.has_value());
            CHECK(*t == doctest::Approx(4.999f));
        }

        SUBCASE("ray outside the quad misses")
        {
            auto t = utilities::ui::intersectRayQuad(
                {20.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}, corners);
            CHECK_FALSE(t.has_value());
        }

        SUBCASE("quad behind the ray origin misses")
        {
            auto t = utilities::ui::intersectRayQuad(
                {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 1.0f}, corners);
            CHECK_FALSE(t.has_value());
        }

        SUBCASE("ray parallel to the quad plane misses")
        {
            auto t = utilities::ui::intersectRayQuad(
                {0.0f, 0.0f, 5.0f}, {1.0f, 0.0f, 0.0f}, corners);
            CHECK_FALSE(t.has_value());
        }
    }

    TEST_CASE("nearer quad yields smaller hit distance (front-most wins)")
    {
        auto canvas = makeCanvas();
        components::UIRectComponent rect;
        rect.anchorMin = {0.0f, 0.0f};
        rect.anchorMax = {1.0f, 1.0f};

        glm::mat4 nearModel = utilities::ui::computeCanvasImageModelMatrix(
            canvas, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 2.0f)), rect);
        glm::mat4 farModel = utilities::ui::computeCanvasImageModelMatrix(
            canvas, glm::mat4(1.0f), rect);

        glm::vec3 nearCorners[4];
        glm::vec3 farCorners[4];
        utilities::ui::computeWorldQuadCorners(nearModel, nearCorners);
        utilities::ui::computeWorldQuadCorners(farModel, farCorners);

        glm::vec3 origin{0.0f, 0.0f, 5.0f};
        glm::vec3 dir{0.0f, 0.0f, -1.0f};
        auto tNear = utilities::ui::intersectRayQuad(origin, dir, nearCorners);
        auto tFar = utilities::ui::intersectRayQuad(origin, dir, farCorners);

        REQUIRE(tNear.has_value());
        REQUIRE(tFar.has_value());
        CHECK(*tNear < *tFar);
    }

    TEST_CASE("pick tie-break: nearest wins, coplanar ties go to smaller area")
    {
        SUBCASE("first hit is always accepted")
        {
            // bestT/bestArea are unset (FLT_MAX) before the first hit; the helper
            // must not derive an epsilon from them (regression: 1e-4 * FLT_MAX)
            CHECK(utilities::ui::isBetterQuadHit(
                false, std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                12.0f, 100.0f));
        }

        SUBCASE("nearer quad beats an earlier farther hit regardless of area")
        {
            // far quad (t=12) accepted first, near quad (t=5) with equal area must win
            CHECK(utilities::ui::isBetterQuadHit(true, 12.0f, 100.0f, 5.0f, 100.0f));
            // even with a larger area
            CHECK(utilities::ui::isBetterQuadHit(true, 12.0f, 100.0f, 5.0f, 500.0f));
        }

        SUBCASE("farther quad never replaces a nearer hit")
        {
            CHECK_FALSE(utilities::ui::isBetterQuadHit(true, 5.0f, 100.0f, 12.0f, 1.0f));
        }

        SUBCASE("coplanar quads tie-break by smaller area")
        {
            CHECK(utilities::ui::isBetterQuadHit(true, 5.0f, 100.0f, 5.0f, 50.0f));
            CHECK_FALSE(utilities::ui::isBetterQuadHit(true, 5.0f, 50.0f, 5.0f, 100.0f));
        }
    }

    TEST_CASE("findCanvasWithEntity walks the parent chain to the canvas")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        scene::Entity canvasEntity("PickTestCanvas");
        canvasEntity.addComponent<components::UICanvasComponent>(makeCanvas());

        scene::Entity child("PickTestImage");
        child.addComponent<components::UIRectComponent>();
        child.addComponent<components::ParentComponent>().parent = canvasEntity.getHandle();

        auto info = utilities::ui::findCanvasWithEntity(registry, child.getHandle());
        REQUIRE(info.canvas != nullptr);
        CHECK(info.canvasEntity == canvasEntity.getHandle());

        // An orphan rect has no canvas and must not be pickable
        scene::Entity orphan("PickTestOrphan");
        orphan.addComponent<components::UIRectComponent>();
        auto orphanInfo = utilities::ui::findCanvasWithEntity(registry, orphan.getHandle());
        CHECK(orphanInfo.canvas == nullptr);

        registry.destroy(child.getHandle());
        registry.destroy(canvasEntity.getHandle());
        registry.destroy(orphan.getHandle());
    }

    // VK-1659: the edit-mode world quad used to centre every element on its anchored point, ignoring the
    // pivot, so Edit and Play only agreed at pivot (0.5, 0.5). It now derives from the Play resolver.
    TEST_CASE("edit-mode quad sits where Play draws the element, for any pivot")
    {
        auto canvas = makeCanvas(); // 1920 x 1080, 100 px per world unit, identity transform
        const float ppu = canvas.pixelsPerUnit;
        const float halfW = canvas.referenceWidth * 0.5f;
        const float halfH = canvas.referenceHeight * 0.5f;

        // World units back to reference px (top-left origin, y down).
        auto toRefX = [&](float worldX) { return worldX * ppu + halfW; };
        auto toRefY = [&](float worldY) { return halfH - worldY * ppu; };

        struct AnchorCase
        {
            glm::vec2 anchorMin;
            glm::vec2 anchorMax;
            glm::vec2 sizeDelta;
        };
        const AnchorCase anchorCases[] = {
            {{0.0f, 1.0f}, {0.0f, 1.0f}, {320.0f, 28.0f}},
            {{0.5f, 0.5f}, {0.5f, 0.5f}, {200.0f, 100.0f}},
            {{1.0f, 0.0f}, {1.0f, 0.0f}, {150.0f, 60.0f}},
            {{0.0f, 0.0f}, {1.0f, 1.0f}, {-40.0f, -60.0f}},
            {{0.2f, 0.1f}, {0.6f, 0.4f}, {30.0f, -10.0f}},
        };
        const float pivots[] = {0.0f, 0.5f, 1.0f};

        for (const AnchorCase& anchors : anchorCases)
        {
            for (float pivotX : pivots)
            {
                for (float pivotY : pivots)
                {
                    components::UIRectComponent rect;
                    rect.anchorMin = anchors.anchorMin;
                    rect.anchorMax = anchors.anchorMax;
                    rect.pivot = {pivotX, pivotY};
                    rect.sizeDelta = anchors.sizeDelta;
                    rect.anchoredPosition = {37.0f, -21.0f};

                    const utilities::ui::PixelRect play = utilities::ui::resolvePixelRect(
                        rect, canvas.referenceWidth, canvas.referenceHeight, 1.0f);

                    glm::vec3 corners[4];
                    utilities::ui::computeWorldQuadCorners(
                        utilities::ui::computeCanvasImageModelMatrix(canvas, glm::mat4(1.0f), rect), corners);

                    CAPTURE(pivotX);
                    CAPTURE(pivotY);
                    // TL
                    CHECK(std::fabs(toRefX(corners[0].x) - play.x) < 1e-3f);
                    CHECK(std::fabs(toRefY(corners[0].y) - play.y) < 1e-3f);
                    // BR
                    CHECK(std::fabs(toRefX(corners[2].x) - (play.x + play.w)) < 1e-3f);
                    CHECK(std::fabs(toRefY(corners[2].y) - (play.y + play.h)) < 1e-3f);

                    // The y-up canvas rect is the same rect seen from the canvas' bottom-left corner.
                    const utilities::ui::CanvasRectYUp up = utilities::ui::computeCanvasRectYUp(canvas, rect);
                    CHECK(up.left == play.x);
                    CHECK(up.bottom == canvas.referenceHeight - (play.y + play.h));
                }
            }
        }
    }

    TEST_CASE("edit-mode quad is unchanged for the default centre pivot")
    {
        auto canvas = makeCanvas();

        // The pre-VK-1659 body of computeCanvasImageModelMatrix (it ignored the pivot).
        auto oldModel = [](const components::UICanvasComponent& c, const components::UIRectComponent& rectComp)
        {
            float canvasW = c.referenceWidth / c.pixelsPerUnit;
            float canvasH = c.referenceHeight / c.pixelsPerUnit;

            float anchorLeft = rectComp.anchorMin.x * c.referenceWidth;
            float anchorRight = rectComp.anchorMax.x * c.referenceWidth;
            float anchorBottom = rectComp.anchorMin.y * c.referenceHeight;
            float anchorTop = rectComp.anchorMax.y * c.referenceHeight;

            float w = (anchorRight - anchorLeft) + rectComp.sizeDelta.x;
            float h = (anchorTop - anchorBottom) + rectComp.sizeDelta.y;
            float cx = (anchorLeft + anchorRight) * 0.5f + rectComp.anchoredPosition.x;
            float cy = (anchorBottom + anchorTop) * 0.5f + rectComp.anchoredPosition.y;

            glm::mat4 canvasScaled = glm::scale(glm::mat4(1.0f), glm::vec3(canvasW, canvasH, 1.0f));
            return canvasScaled
                * glm::translate(glm::mat4(1.0f), glm::vec3(cx / c.referenceWidth - 0.5f,
                                                            cy / c.referenceHeight - 0.5f, 0.001f))
                * glm::scale(glm::mat4(1.0f), glm::vec3(w / c.referenceWidth, h / c.referenceHeight, 1.0f));
        };

        const glm::vec2 anchorSets[][2] = {
            {{0.0f, 0.0f}, {1.0f, 1.0f}},
            {{0.5f, 0.5f}, {0.5f, 0.5f}},
            {{0.0f, 1.0f}, {0.0f, 1.0f}},
            {{0.25f, 0.1f}, {0.75f, 0.3f}},
        };
        for (const auto& anchors : anchorSets)
        {
            components::UIRectComponent rect; // pivot (0.5, 0.5)
            rect.anchorMin = anchors[0];
            rect.anchorMax = anchors[1];
            rect.sizeDelta = {240.0f, 90.0f};
            rect.anchoredPosition = {-55.0f, 33.0f};

            glm::vec3 now[4];
            glm::vec3 before[4];
            utilities::ui::computeWorldQuadCorners(
                utilities::ui::computeCanvasImageModelMatrix(canvas, glm::mat4(1.0f), rect), now);
            utilities::ui::computeWorldQuadCorners(oldModel(canvas, rect), before);
            for (int i = 0; i < 4; ++i)
            {
                CHECK(now[i].x == doctest::Approx(before[i].x).epsilon(1e-5));
                CHECK(now[i].y == doctest::Approx(before[i].y).epsilon(1e-5));
                CHECK(now[i].z == doctest::Approx(before[i].z).epsilon(1e-5));
            }
        }
    }

    TEST_CASE("a top-left pivot no longer shifts the edit-mode quad by half its size")
    {
        auto canvas = makeCanvas();
        components::UIRectComponent rect;
        rect.anchorMin = {0.0f, 1.0f};
        rect.anchorMax = {0.0f, 1.0f};
        rect.pivot = {0.0f, 0.0f}; // pivot.y is measured from the TOP edge
        rect.sizeDelta = {320.0f, 28.0f};
        rect.anchoredPosition = {24.0f, -24.0f}; // Play: the rect (24, 24, 320, 28) at the top-left

        glm::vec3 corners[4];
        utilities::ui::computeWorldQuadCorners(
            utilities::ui::computeCanvasImageModelMatrix(canvas, glm::mat4(1.0f), rect), corners);

        // TL at reference px (24, 24): world ((24 - 960) / 100, (540 - 24) / 100)
        CHECK(corners[0].x == doctest::Approx(-9.36f));
        CHECK(corners[0].y == doctest::Approx(5.16f));
        // BR at reference px (344, 52)
        CHECK(corners[2].x == doctest::Approx(-6.16f));
        CHECK(corners[2].y == doctest::Approx(4.88f));
    }

    TEST_CASE("debug outlines lift the same quad to their own z offset")
    {
        auto canvas = makeCanvas();
        components::UIRectComponent rect;
        rect.anchorMin = {0.5f, 0.5f};
        rect.anchorMax = {0.5f, 0.5f};
        rect.pivot = {1.0f, 0.0f};
        rect.sizeDelta = {200.0f, 100.0f};

        glm::vec3 image[4];
        glm::vec3 outline[4];
        utilities::ui::computeWorldQuadCorners(
            utilities::ui::computeCanvasImageModelMatrix(canvas, glm::mat4(1.0f), rect), image);
        utilities::ui::computeWorldQuadCorners(
            utilities::ui::computeCanvasImageModelMatrix(canvas, glm::mat4(1.0f), rect, 0.002f), outline);
        for (int i = 0; i < 4; ++i)
        {
            CHECK(outline[i].x == doctest::Approx(image[i].x));
            CHECK(outline[i].y == doctest::Approx(image[i].y));
            CHECK(outline[i].z == doctest::Approx(0.002f));
        }
    }
}
