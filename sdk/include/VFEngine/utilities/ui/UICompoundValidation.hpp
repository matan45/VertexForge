#pragma once

// VK-1442 — compound-widget structure validation (header-only, CPU-only).
//
// A "compound widget" (Tabs, Dropdown, Tooltip, Checkbox) is only meaningful when its
// supporting child composition is well-formed: a tabs container needs a tab bar plus one
// content pane per tab button, a dropdown needs at least one option, a child-panel tooltip
// needs a panel to show, etc. This validator inspects the live registry subtree and reports
// structural errors / advisory warnings so the UI Layer Builder inspector can surface a
// non-blocking strip. It is registry-based and inline so it is trivially unit-testable and
// usable from the editor without any service/CQRS dependency.
//
// It only ever READS the registry. All mutations stay on the CQRS command path.

#include <entt/entt.hpp>
#include <string>
#include <vector>

#include "../components/CoreComponents.hpp" // ChildrenComponent, NameComponent
#include "../components/UIComponents.hpp"   // UI*Component types

namespace ui_validation
{
    struct CompoundWidgetStatus
    {
        bool ok = true;
        std::vector<std::string> errors;   // structural problems (the widget won't behave)
        std::vector<std::string> warnings; // advisory (works, but probably not intended)
    };

    namespace detail
    {
        // Direct, still-valid children of `e` in hierarchy order (empty if none).
        inline std::vector<entt::entity> validChildren(const entt::registry& reg, entt::entity e)
        {
            std::vector<entt::entity> out;
            if (const auto* children = reg.try_get<components::ChildrenComponent>(e))
            {
                out.reserve(children->children.size());
                for (entt::entity c : children->children)
                    if (reg.valid(c))
                        out.push_back(c);
            }
            return out;
        }

        inline bool isInactive(const entt::registry& reg, entt::entity e)
        {
            const auto* name = reg.try_get<components::NameComponent>(e);
            return name && !name->isActive;
        }
    }

    // Validates the compound widget(s) on `e`. Each check is gated on the relevant component
    // being present, so a non-compound entity simply returns an ok/empty status. An entity that
    // carries several UI components is checked for each (rare, but harmless).
    inline CompoundWidgetStatus validateCompoundWidget(entt::registry& reg, entt::entity e)
    {
        CompoundWidgetStatus status;
        auto addError = [&](std::string msg)
        {
            status.ok = false;
            status.errors.push_back(std::move(msg));
        };
        auto addWarning = [&](std::string msg) { status.warnings.push_back(std::move(msg)); };

        if (!reg.valid(e))
            return status;

        const std::vector<entt::entity> children = detail::validChildren(reg, e);

        // ---- Tabs --------------------------------------------------------------
        // Required shape: the FIRST child carries a UILayoutGroupComponent (the tab bar);
        // every OTHER direct child is a content pane; tab-button count (the bar's children)
        // equals pane count; at least one pane exists; activeTabIndex is in [0, paneCount).
        if (const auto* tabs = reg.try_get<components::UITabsComponent>(e))
        {
            entt::entity tabBar = entt::null;
            if (!children.empty() && reg.all_of<components::UILayoutGroupComponent>(children.front()))
                tabBar = children.front();

            if (tabBar == entt::null)
                addError("Tabs: the first child must be the tab bar (a UI Layout Group).");

            int paneCount = 0;
            for (entt::entity c : children)
                if (c != tabBar)
                    ++paneCount;

            int tabButtonCount = 0;
            if (tabBar != entt::null)
                tabButtonCount = static_cast<int>(detail::validChildren(reg, tabBar).size());

            if (paneCount < 1)
                addError("Tabs: needs at least one content pane.");

            if (tabBar != entt::null && tabButtonCount != paneCount)
                addError("Tabs: tab-button count (" + std::to_string(tabButtonCount) +
                         ") must equal content-pane count (" + std::to_string(paneCount) + ").");

            if (paneCount > 0 && (tabs->activeTabIndex < 0 || tabs->activeTabIndex >= paneCount))
                addError("Tabs: active tab index " + std::to_string(tabs->activeTabIndex) +
                         " is out of range [0, " + std::to_string(paneCount) + ").");
        }

        // ---- Dropdown ----------------------------------------------------------
        if (const auto* dropdown = reg.try_get<components::UIDropdownComponent>(e))
        {
            if (dropdown->options.empty())
                addError("Dropdown: the option list is empty.");
        }

        // ---- Tooltip -----------------------------------------------------------
        // Child-panel mode needs a target: a named child (panelChildName) or, when the name is
        // blank, an inactive child carrying a UIRect (the convention the runtime resolves to).
        if (const auto* tooltip = reg.try_get<components::UITooltipComponent>(e))
        {
            if (tooltip->mode == components::UITooltipMode::ChildPanel)
            {
                bool found = false;
                if (!tooltip->panelChildName.empty())
                {
                    for (entt::entity c : children)
                    {
                        const auto* name = reg.try_get<components::NameComponent>(c);
                        if (name && name->name == tooltip->panelChildName)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        addError("Tooltip: panel child \"" + tooltip->panelChildName +
                                 "\" was not found among the children.");
                }
                else
                {
                    for (entt::entity c : children)
                    {
                        if (detail::isInactive(reg, c) && reg.all_of<components::UIRectComponent>(c))
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        addError("Tooltip: Child Panel mode needs an inactive child panel (with a "
                                 "UI Rect), or set a Panel Child name.");
                }
            }
        }

        // ---- Checkbox ----------------------------------------------------------
        if (const auto* checkbox = reg.try_get<components::UICheckboxComponent>(e))
        {
            if (checkbox->labelToggle)
            {
                bool hasLabel = false;
                for (entt::entity c : children)
                {
                    if (reg.all_of<components::UILabelComponent>(c))
                    {
                        hasLabel = true;
                        break;
                    }
                }
                if (!hasLabel)
                    addWarning("Checkbox: 'Label Toggle' is enabled but there is no child UI Label "
                               "to click.");
            }
        }

        return status;
    }
}
