#include <doctest.h>
#include <components/Components.hpp>
#include <scene/EntityRegistry.hpp>
#include <ui/UITheme.hpp>
#include <ui/UIThemeApplier.hpp>
#include <ui/UIThemeSerialization.hpp>
#include <entt/entt.hpp>

// UI theme system: .vfTheme JSON round-trip and the apply pass that writes
// style properties into concrete component fields per canvas subtree.

namespace
{
    entt::entity makeChild(entt::registry& registry, entt::entity parent)
    {
        entt::entity child = registry.create();
        registry.emplace<components::ParentComponent>(child).parent = parent;
        auto* children = registry.try_get<components::ChildrenComponent>(parent);
        if (!children)
        {
            children = &registry.emplace<components::ChildrenComponent>(parent);
        }
        children->children.push_back(child);
        return child;
    }
}

TEST_SUITE("UITheme")
{
    TEST_CASE("theme JSON round-trip preserves styles and properties")
    {
        utilities::ui::UITheme theme;
        utilities::ui::UIThemeStyle heading;
        heading.colors["labelColor"] = {1.0f, 0.5f, 0.25f, 1.0f};
        heading.floats["fontSize"] = 32.0f;
        theme.styles["Heading"] = heading;

        utilities::ui::UIThemeStyle button;
        button.colors["buttonNormalColor"] = {0.1f, 0.2f, 0.3f, 1.0f};
        button.colors["buttonHoveredColor"] = {0.2f, 0.3f, 0.4f, 1.0f};
        theme.styles["ActionButton"] = button;

        auto j = utilities::ui::UIThemeSerialization::toJson(theme);
        auto loaded = utilities::ui::UIThemeSerialization::fromJson(j);

        REQUIRE(loaded.styles.size() == 2);
        const auto* h = loaded.findStyle("Heading");
        REQUIRE(h != nullptr);
        CHECK(h->colors.at("labelColor").r == doctest::Approx(1.0f));
        CHECK(h->colors.at("labelColor").g == doctest::Approx(0.5f));
        CHECK(h->floats.at("fontSize") == doctest::Approx(32.0f));

        const auto* b = loaded.findStyle("ActionButton");
        REQUIRE(b != nullptr);
        CHECK(b->colors.at("buttonNormalColor").b == doctest::Approx(0.3f));
        CHECK(b->colors.at("buttonHoveredColor").g == doctest::Approx(0.3f));
        CHECK(loaded.findStyle("Missing") == nullptr);
    }

    TEST_CASE("malformed theme JSON yields an empty theme")
    {
        auto loaded = utilities::ui::UIThemeSerialization::fromJson(nlohmann::json::object());
        CHECK(loaded.styles.empty());

        nlohmann::json badStyles;
        badStyles["styles"] = 42;
        loaded = utilities::ui::UIThemeSerialization::fromJson(badStyles);
        CHECK(loaded.styles.empty());
    }

    TEST_CASE("applyTheme writes style properties into the canvas subtree")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        entt::entity canvas = registry.create();
        registry.emplace<components::UICanvasComponent>(canvas);

        entt::entity labelEntity = makeChild(registry, canvas);
        auto& label = registry.emplace<components::UILabelComponent>(labelEntity);
        label.color = {1.0f, 1.0f, 1.0f, 1.0f};
        label.fontSize = 16.0f;
        registry.emplace<components::UIStyleComponent>(labelEntity).styleKey = "Heading";

        entt::entity buttonEntity = makeChild(registry, labelEntity); // nested child
        auto& button = registry.emplace<components::UIButtonComponent>(buttonEntity);
        registry.emplace<components::UIStyleComponent>(buttonEntity).styleKey = "ActionButton";

        // Unstyled sibling must stay untouched
        entt::entity plainEntity = makeChild(registry, canvas);
        auto& plain = registry.emplace<components::UILabelComponent>(plainEntity);
        plain.fontSize = 11.0f;

        utilities::ui::UITheme theme;
        utilities::ui::UIThemeStyle heading;
        heading.colors["labelColor"] = {0.9f, 0.1f, 0.2f, 1.0f};
        heading.floats["fontSize"] = 28.0f;
        theme.styles["Heading"] = heading;

        utilities::ui::UIThemeStyle actionButton;
        actionButton.colors["buttonNormalColor"] = {0.0f, 0.4f, 0.8f, 1.0f};
        theme.styles["ActionButton"] = actionButton;

        int touched = utilities::ui::UIThemeApplier::applyTheme(registry, theme, canvas);
        CHECK(touched == 2);

        const auto& appliedLabel = registry.get<components::UILabelComponent>(labelEntity);
        CHECK(appliedLabel.color.r == doctest::Approx(0.9f));
        CHECK(appliedLabel.color.g == doctest::Approx(0.1f));
        CHECK(appliedLabel.fontSize == doctest::Approx(28.0f));

        const auto& appliedButton = registry.get<components::UIButtonComponent>(buttonEntity);
        CHECK(appliedButton.normalColor.b == doctest::Approx(0.8f));
        // Properties not present in the style keep their authored values
        CHECK(appliedButton.hoveredColor.r == doctest::Approx(0.9f));

        const auto& untouched = registry.get<components::UILabelComponent>(plainEntity);
        CHECK(untouched.fontSize == doctest::Approx(11.0f));

        registry.destroy(plainEntity);
        registry.destroy(buttonEntity);
        registry.destroy(labelEntity);
        registry.destroy(canvas);
    }

    TEST_CASE("missing style key is a no-op")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        entt::entity canvas = registry.create();
        registry.emplace<components::UICanvasComponent>(canvas);

        entt::entity labelEntity = makeChild(registry, canvas);
        auto& label = registry.emplace<components::UILabelComponent>(labelEntity);
        label.fontSize = 16.0f;
        registry.emplace<components::UIStyleComponent>(labelEntity).styleKey = "DoesNotExist";

        utilities::ui::UITheme theme;
        theme.styles["SomethingElse"] = {};

        int touched = utilities::ui::UIThemeApplier::applyTheme(registry, theme, canvas);
        CHECK(touched == 0);
        CHECK(registry.get<components::UILabelComponent>(labelEntity).fontSize == doctest::Approx(16.0f));

        registry.destroy(labelEntity);
        registry.destroy(canvas);
    }

    TEST_CASE("apply is scoped to the given canvas subtree")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        entt::entity canvasA = registry.create();
        registry.emplace<components::UICanvasComponent>(canvasA);
        entt::entity labelA = makeChild(registry, canvasA);
        registry.emplace<components::UILabelComponent>(labelA).fontSize = 10.0f;
        registry.emplace<components::UIStyleComponent>(labelA).styleKey = "Shared";

        entt::entity canvasB = registry.create();
        registry.emplace<components::UICanvasComponent>(canvasB);
        entt::entity labelB = makeChild(registry, canvasB);
        registry.emplace<components::UILabelComponent>(labelB).fontSize = 10.0f;
        registry.emplace<components::UIStyleComponent>(labelB).styleKey = "Shared";

        utilities::ui::UITheme theme;
        utilities::ui::UIThemeStyle shared;
        shared.floats["fontSize"] = 99.0f;
        theme.styles["Shared"] = shared;

        utilities::ui::UIThemeApplier::applyTheme(registry, theme, canvasA);

        CHECK(registry.get<components::UILabelComponent>(labelA).fontSize == doctest::Approx(99.0f));
        CHECK(registry.get<components::UILabelComponent>(labelB).fontSize == doctest::Approx(10.0f));

        registry.destroy(labelA);
        registry.destroy(canvasA);
        registry.destroy(labelB);
        registry.destroy(canvasB);
    }
}
