#include <doctest.h>
#include <components/Components.hpp>
#include <ui/UIRectMath.hpp>
#include <scene/EntityRegistry.hpp>
#include <entt/entt.hpp>

// Tooltip placement math (computeTooltipPlacement) and the component's
// serialization-facing defaults. The hover state machine itself runs inside
// the graphics interaction system (not linked here); its placement math is
// the pure part under test.

TEST_SUITE("UITooltip")
{
    TEST_CASE("placement: default below-right of the anchor")
    {
        glm::vec2 pos = utilities::ui::computeTooltipPlacement(
            {100.0f, 100.0f}, {12.0f, 16.0f}, {200.0f, 50.0f}, 1920.0f, 1080.0f);
        CHECK(pos.x == doctest::Approx(112.0f));
        CHECK(pos.y == doctest::Approx(116.0f));
    }

    TEST_CASE("placement: flips above the anchor near the bottom edge")
    {
        glm::vec2 pos = utilities::ui::computeTooltipPlacement(
            {100.0f, 1060.0f}, {12.0f, 16.0f}, {200.0f, 50.0f}, 1920.0f, 1080.0f);
        // 1060 + 16 + 50 > 1080 -> flip: 1060 - 16 - 50 = 994
        CHECK(pos.y == doctest::Approx(994.0f));
        CHECK(pos.x == doctest::Approx(112.0f));
    }

    TEST_CASE("placement: clamps to the right edge")
    {
        glm::vec2 pos = utilities::ui::computeTooltipPlacement(
            {1900.0f, 100.0f}, {12.0f, 16.0f}, {200.0f, 50.0f}, 1920.0f, 1080.0f);
        CHECK(pos.x == doctest::Approx(1720.0f)); // 1920 - 200
    }

    TEST_CASE("placement: never goes negative")
    {
        glm::vec2 pos = utilities::ui::computeTooltipPlacement(
            {5.0f, 5.0f}, {-50.0f, -300.0f}, {200.0f, 50.0f}, 1920.0f, 1080.0f);
        CHECK(pos.x >= 0.0f);
        CHECK(pos.y >= 0.0f);
    }

    TEST_CASE("placement: flip that would overshoot the top clamps to 0")
    {
        // Anchor near the bottom with a tooltip taller than the space above
        glm::vec2 pos = utilities::ui::computeTooltipPlacement(
            {100.0f, 90.0f}, {0.0f, 20.0f}, {200.0f, 100.0f}, 1920.0f, 100.0f);
        // below: 90+20+100 > 100 -> flip: 90-20-100 = -30 -> clamp 0
        CHECK(pos.y == doctest::Approx(0.0f));
    }

    TEST_CASE("component defaults are sane")
    {
        components::UITooltipComponent tip;
        CHECK(tip.mode == components::UITooltipMode::Text);
        CHECK(tip.showDelay == doctest::Approx(0.5f));
        CHECK(tip.followCursor);
        CHECK(tip.enabled);
        CHECK(tip.maxWidth > 0.0f);
    }

    TEST_CASE("tooltip state singleton default-constructs hidden")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        registry.ctx().erase<components::UITooltipState>();
        auto& state = registry.ctx().emplace<components::UITooltipState>();
        CHECK_FALSE(state.visible);
        CHECK(state.hoveredEntity == entt::null);
        CHECK(state.hoverTime == doctest::Approx(0.0f));
    }
}
