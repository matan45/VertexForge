#include <doctest.h>
#include <components/Components.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <ui/UIRectMath.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

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
}
