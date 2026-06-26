#include <doctest.h>

// VK-1442 — compound-widget structure validation (CPU-only).
//
// Exercises ui_validation::validateCompoundWidget against hand-built registries: the four
// compound widgets (Tabs, Dropdown, Tooltip, Checkbox) and their well-formed / malformed
// child compositions. No graphics, no CQRS — the validator is a pure registry read, so a
// local entt::registry is all that is needed.

#include <ui/UICompoundValidation.hpp>

#include <components/CoreComponents.hpp>
#include <components/UIComponents.hpp>
#include <entt/entt.hpp>

namespace
{
    // Create a named entity (NameComponent.isActive defaults to true).
    entt::entity makeEntity(entt::registry& reg, const std::string& name, bool active = true)
    {
        entt::entity e = reg.create();
        auto& nc = reg.emplace<components::NameComponent>(e);
        nc.name = name;
        nc.isActive = active;
        return e;
    }

    // Append `child` to `parent`'s ChildrenComponent (creating it on first use).
    void attach(entt::registry& reg, entt::entity parent, entt::entity child)
    {
        reg.emplace_or_replace<components::ParentComponent>(child).parent = parent;
        auto* children = reg.try_get<components::ChildrenComponent>(parent);
        if (!children)
            children = &reg.emplace<components::ChildrenComponent>(parent);
        children->children.push_back(child);
    }

    // Build a tabs root with `tabButtons` buttons under a tab bar and `panes` content panes.
    // Returns the tabs root. activeTabIndex is configurable so out-of-range can be tested.
    entt::entity buildTabs(entt::registry& reg, int tabButtons, int panes, int activeTabIndex,
                           bool includeTabBar = true)
    {
        entt::entity root = makeEntity(reg, "Tabs");
        reg.emplace<components::UIRectComponent>(root);
        auto& tabs = reg.emplace<components::UITabsComponent>(root);
        tabs.activeTabIndex = activeTabIndex;

        if (includeTabBar)
        {
            entt::entity bar = makeEntity(reg, "TabBar");
            reg.emplace<components::UIRectComponent>(bar);
            reg.emplace<components::UILayoutGroupComponent>(bar).direction =
                components::LayoutDirection::Horizontal;
            attach(reg, root, bar);

            for (int i = 0; i < tabButtons; ++i)
            {
                entt::entity btn = makeEntity(reg, "Tab " + std::to_string(i + 1));
                reg.emplace<components::UIRectComponent>(btn);
                reg.emplace<components::UIImageComponent>(btn);
                attach(reg, bar, btn);
            }
        }

        for (int i = 0; i < panes; ++i)
        {
            entt::entity pane = makeEntity(reg, "Pane " + std::to_string(i + 1), i == activeTabIndex);
            reg.emplace<components::UIRectComponent>(pane);
            reg.emplace<components::UIImageComponent>(pane);
            attach(reg, root, pane);
        }
        return root;
    }
}

TEST_SUITE("CompoundWidgetValidation")
{
    // -------------------------------- Tabs ----------------------------------

    TEST_CASE("valid tabs (bar + matching buttons/panes, in-range active) passes")
    {
        entt::registry reg;
        entt::entity root = buildTabs(reg, /*tabButtons*/ 2, /*panes*/ 2, /*activeTabIndex*/ 0);

        auto status = ui_validation::validateCompoundWidget(reg, root);
        CHECK(status.ok);
        CHECK(status.errors.empty());
    }

    TEST_CASE("tabs with no tab bar reports an error")
    {
        entt::registry reg;
        // No tab bar: first child is a plain pane, so children.front() lacks a UILayoutGroup.
        entt::entity root = buildTabs(reg, /*tabButtons*/ 0, /*panes*/ 2, /*activeTabIndex*/ 0,
                                      /*includeTabBar*/ false);

        auto status = ui_validation::validateCompoundWidget(reg, root);
        CHECK_FALSE(status.ok);
        CHECK_FALSE(status.errors.empty());
    }

    TEST_CASE("tabs with button/pane count mismatch reports an error")
    {
        entt::registry reg;
        entt::entity root = buildTabs(reg, /*tabButtons*/ 3, /*panes*/ 2, /*activeTabIndex*/ 0);

        auto status = ui_validation::validateCompoundWidget(reg, root);
        CHECK_FALSE(status.ok);
        CHECK_FALSE(status.errors.empty());
    }

    TEST_CASE("tabs with no panes reports an error")
    {
        entt::registry reg;
        entt::entity root = buildTabs(reg, /*tabButtons*/ 0, /*panes*/ 0, /*activeTabIndex*/ 0);

        auto status = ui_validation::validateCompoundWidget(reg, root);
        CHECK_FALSE(status.ok);
    }

    TEST_CASE("tabs with out-of-range active index reports an error")
    {
        entt::registry reg;
        entt::entity root = buildTabs(reg, /*tabButtons*/ 2, /*panes*/ 2, /*activeTabIndex*/ 5);

        auto status = ui_validation::validateCompoundWidget(reg, root);
        CHECK_FALSE(status.ok);
    }

    // ------------------------------ Dropdown --------------------------------

    TEST_CASE("dropdown with options passes; empty dropdown fails")
    {
        entt::registry reg;

        entt::entity good = makeEntity(reg, "Dropdown");
        reg.emplace<components::UIRectComponent>(good);
        auto& dd = reg.emplace<components::UIDropdownComponent>(good);
        dd.options.push_back({"Option 1", {}});
        dd.options.push_back({"Option 2", {}});

        auto okStatus = ui_validation::validateCompoundWidget(reg, good);
        CHECK(okStatus.ok);
        CHECK(okStatus.errors.empty());

        entt::entity empty = makeEntity(reg, "EmptyDropdown");
        reg.emplace<components::UIRectComponent>(empty);
        reg.emplace<components::UIDropdownComponent>(empty); // no options

        auto badStatus = ui_validation::validateCompoundWidget(reg, empty);
        CHECK_FALSE(badStatus.ok);
        CHECK_FALSE(badStatus.errors.empty());
    }

    // ------------------------------- Tooltip --------------------------------

    TEST_CASE("text-mode tooltip never needs a child panel")
    {
        entt::registry reg;
        entt::entity host = makeEntity(reg, "TooltipHost");
        reg.emplace<components::UIRectComponent>(host);
        auto& tip = reg.emplace<components::UITooltipComponent>(host);
        tip.mode = components::UITooltipMode::Text;
        tip.text = "Tooltip text";

        auto status = ui_validation::validateCompoundWidget(reg, host);
        CHECK(status.ok);
        CHECK(status.errors.empty());
    }

    TEST_CASE("child-panel tooltip with a missing target reports an error")
    {
        entt::registry reg;
        entt::entity host = makeEntity(reg, "TooltipHost");
        reg.emplace<components::UIRectComponent>(host);
        auto& tip = reg.emplace<components::UITooltipComponent>(host);
        tip.mode = components::UITooltipMode::ChildPanel;
        tip.panelChildName = "Panel"; // no such child exists

        auto status = ui_validation::validateCompoundWidget(reg, host);
        CHECK_FALSE(status.ok);
    }

    TEST_CASE("child-panel tooltip resolves a named child")
    {
        entt::registry reg;
        entt::entity host = makeEntity(reg, "TooltipHost");
        reg.emplace<components::UIRectComponent>(host);
        auto& tip = reg.emplace<components::UITooltipComponent>(host);
        tip.mode = components::UITooltipMode::ChildPanel;
        tip.panelChildName = "Panel";

        entt::entity panel = makeEntity(reg, "Panel", /*active*/ false);
        reg.emplace<components::UIRectComponent>(panel);
        attach(reg, host, panel);

        auto status = ui_validation::validateCompoundWidget(reg, host);
        CHECK(status.ok);
        CHECK(status.errors.empty());
    }

    TEST_CASE("child-panel tooltip with blank name resolves the first inactive UIRect child")
    {
        entt::registry reg;
        entt::entity host = makeEntity(reg, "TooltipHost");
        reg.emplace<components::UIRectComponent>(host);
        auto& tip = reg.emplace<components::UITooltipComponent>(host);
        tip.mode = components::UITooltipMode::ChildPanel;
        tip.panelChildName.clear();

        entt::entity panel = makeEntity(reg, "HiddenPanel", /*active*/ false);
        reg.emplace<components::UIRectComponent>(panel);
        attach(reg, host, panel);

        auto status = ui_validation::validateCompoundWidget(reg, host);
        CHECK(status.ok);
    }

    // ------------------------------- Checkbox -------------------------------

    TEST_CASE("checkbox with label toggle and a child label passes")
    {
        entt::registry reg;
        entt::entity root = makeEntity(reg, "Checkbox");
        reg.emplace<components::UIRectComponent>(root);
        reg.emplace<components::UIImageComponent>(root);
        auto& cb = reg.emplace<components::UICheckboxComponent>(root);
        cb.labelToggle = true;

        entt::entity label = makeEntity(reg, "Label");
        reg.emplace<components::UIRectComponent>(label);
        reg.emplace<components::UILabelComponent>(label);
        attach(reg, root, label);

        auto status = ui_validation::validateCompoundWidget(reg, root);
        CHECK(status.ok);
        CHECK(status.warnings.empty());
    }

    TEST_CASE("checkbox with label toggle but no child label warns (still ok)")
    {
        entt::registry reg;
        entt::entity root = makeEntity(reg, "Checkbox");
        reg.emplace<components::UIRectComponent>(root);
        reg.emplace<components::UIImageComponent>(root);
        auto& cb = reg.emplace<components::UICheckboxComponent>(root);
        cb.labelToggle = true; // no child label attached

        auto status = ui_validation::validateCompoundWidget(reg, root);
        CHECK(status.ok); // a warning is non-blocking
        CHECK_FALSE(status.warnings.empty());
    }

    // ----------------------------- Non-compound -----------------------------

    TEST_CASE("a plain entity with no compound components yields an empty status")
    {
        entt::registry reg;
        entt::entity e = makeEntity(reg, "Plain");
        reg.emplace<components::UIRectComponent>(e);

        auto status = ui_validation::validateCompoundWidget(reg, e);
        CHECK(status.ok);
        CHECK(status.errors.empty());
        CHECK(status.warnings.empty());
    }
}
