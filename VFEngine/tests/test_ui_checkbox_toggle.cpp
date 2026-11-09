#include <doctest.h>
#include <components/Components.hpp>
#include <ui/UICheckboxToggle.hpp>
#include <entt/entt.hpp>

#include <string>
#include <vector>

// VK-1659: the checkbox click rule shared by the runtime pointer path (UIInteractionSystem) and
// SimulateUIClickCommand (MCP ui_click). The callback must run exactly where the runtime publishes
// UICheckboxToggledNotification: each radio peer as it is unchecked, then the clicked box last.

namespace
{
    struct Toggle
    {
        entt::entity entity;
        bool newChecked;
        bool previousChecked;
    };

    entt::entity makeBox(entt::registry& registry, const std::string& group, bool checked,
                         bool allowUncheck = true, bool withRect = true, bool active = true)
    {
        entt::entity e = registry.create();
        auto& name = registry.emplace<components::NameComponent>(e);
        name.name = "Box";
        name.isActive = active;
        auto& box = registry.emplace<components::UICheckboxComponent>(e);
        box.groupName = group;
        box.isChecked = checked;
        box.allowUncheck = allowUncheck;
        if (withRect)
            registry.emplace<components::UIRectComponent>(e);
        return e;
    }

    std::vector<Toggle> click(entt::registry& registry, entt::entity box)
    {
        std::vector<Toggle> toggles;
        utilities::ui::applyCheckboxClick(registry, box,
            [&](entt::entity entity, bool newChecked, bool previousChecked)
            {
                toggles.push_back({entity, newChecked, previousChecked});
            });
        return toggles;
    }

    bool isChecked(const entt::registry& registry, entt::entity e)
    {
        return registry.get<components::UICheckboxComponent>(e).isChecked;
    }
}

TEST_SUITE("UICheckboxToggle")
{
    TEST_CASE("an independent checkbox flips on every click")
    {
        entt::registry registry;
        const entt::entity box = makeBox(registry, "", false);

        std::vector<Toggle> first = click(registry, box);
        CHECK(isChecked(registry, box));
        REQUIRE(first.size() == 1);
        CHECK(first[0].entity == box);
        CHECK(first[0].newChecked == true);
        CHECK(first[0].previousChecked == false);

        std::vector<Toggle> second = click(registry, box);
        CHECK_FALSE(isChecked(registry, box));
        REQUIRE(second.size() == 1);
        CHECK(second[0].newChecked == false);
        CHECK(second[0].previousChecked == true);
    }

    TEST_CASE("checking a radio box unchecks its checked peers first, then reports itself")
    {
        entt::registry registry;
        const entt::entity a = makeBox(registry, "g", false);
        const entt::entity b = makeBox(registry, "g", true);
        const entt::entity c = makeBox(registry, "g", false);
        const entt::entity d = makeBox(registry, "g", true); // a second checked peer (inconsistent, still handled)

        // The peers come in the order of the view the runtime iterates.
        std::vector<entt::entity> expectedPeers;
        for (auto e : registry.view<components::UICheckboxComponent, components::UIRectComponent>())
        {
            if (e != a && registry.get<components::UICheckboxComponent>(e).isChecked)
                expectedPeers.push_back(e);
        }
        REQUIRE(expectedPeers.size() == 2);

        const std::vector<Toggle> toggles = click(registry, a);
        CHECK(isChecked(registry, a));
        CHECK_FALSE(isChecked(registry, b));
        CHECK_FALSE(isChecked(registry, c));
        CHECK_FALSE(isChecked(registry, d));

        REQUIRE(toggles.size() == 3);
        for (std::size_t i = 0; i < expectedPeers.size(); ++i)
        {
            CHECK(toggles[i].entity == expectedPeers[i]);
            CHECK(toggles[i].newChecked == false);
            CHECK(toggles[i].previousChecked == true);
        }
        CHECK(toggles[2].entity == a);
        CHECK(toggles[2].newChecked == true);
        CHECK(toggles[2].previousChecked == false);
    }

    TEST_CASE("the clicked radio box is already checked when its peers are reported")
    {
        entt::registry registry;
        const entt::entity a = makeBox(registry, "g", false);
        const entt::entity b = makeBox(registry, "g", true);

        bool clickedWasCheckedAtPeerCallback = false;
        utilities::ui::applyCheckboxClick(registry, a,
            [&](entt::entity entity, bool, bool)
            {
                if (entity == b)
                    clickedWasCheckedAtPeerCallback = isChecked(registry, a);
            });
        CHECK(clickedWasCheckedAtPeerCallback);
    }

    TEST_CASE("a strict radio box stays checked and reports nothing")
    {
        entt::registry registry;
        const entt::entity a = makeBox(registry, "g", true, /*allowUncheck*/ false);
        const entt::entity b = makeBox(registry, "g", false);

        const std::vector<Toggle> toggles = click(registry, a);
        CHECK(toggles.empty());
        CHECK(isChecked(registry, a));
        CHECK_FALSE(isChecked(registry, b));
    }

    TEST_CASE("a checked radio box with allowUncheck unchecks only itself")
    {
        entt::registry registry;
        const entt::entity a = makeBox(registry, "g", true, /*allowUncheck*/ true);
        const entt::entity b = makeBox(registry, "g", false);

        const std::vector<Toggle> toggles = click(registry, a);
        CHECK_FALSE(isChecked(registry, a));
        CHECK_FALSE(isChecked(registry, b));
        REQUIRE(toggles.size() == 1);
        CHECK(toggles[0].entity == a);
        CHECK(toggles[0].newChecked == false);
        CHECK(toggles[0].previousChecked == true);
    }

    TEST_CASE("other groups, inactive peers and rect-less boxes follow the runtime rule")
    {
        entt::registry registry;
        const entt::entity a = makeBox(registry, "g", false);
        const entt::entity otherGroup = makeBox(registry, "h", true);
        const entt::entity independent = makeBox(registry, "", true);
        const entt::entity inactivePeer = makeBox(registry, "g", true, true, true, /*active*/ false);
        const entt::entity rectlessPeer = makeBox(registry, "g", true, true, /*withRect*/ false);

        const std::vector<Toggle> toggles = click(registry, a);

        CHECK(isChecked(registry, a));
        CHECK(isChecked(registry, otherGroup));   // another group is untouched
        CHECK(isChecked(registry, independent));  // an independent box is untouched
        CHECK_FALSE(isChecked(registry, inactivePeer)); // the rule ignores the active flag
        CHECK(isChecked(registry, rectlessPeer));  // not in the runtime's view<UICheckbox, UIRect>

        REQUIRE(toggles.size() == 2);
        CHECK(toggles[0].entity == inactivePeer);
        CHECK(toggles[1].entity == a);
    }
}
