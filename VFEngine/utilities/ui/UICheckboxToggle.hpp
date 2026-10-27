#pragma once
#include "../components/UIComponents.hpp"
#include <entt/entt.hpp>
#include <string>

// VK-1659: the checkbox click rule, shared by the runtime pointer path (UIInteractionSystem's
// processCheckboxInteraction) and SimulateUIClickCommand (the MCP ui_click tool), so a simulated click
// changes exactly what a real one does. Header-only; touches only the registry.
namespace utilities::ui
{
    // Applies one click to `checkbox` the way a left-button release over it does:
    //   - independent box (empty groupName): flips isChecked;
    //   - radio box, unchecked: checks it, then unchecks every OTHER checked box with the same groupName
    //     (every UICheckbox + UIRect entity in the registry, active or not);
    //   - radio box, checked: unchecks it when allowUncheck, otherwise changes nothing.
    // onToggle(entity, newChecked, previousChecked) runs exactly where the runtime publishes
    // UICheckboxToggledNotification: for each peer right after it is unchecked, then for the clicked box
    // last (not at all when nothing changed). Gating (interactable, active, modal, hover) is the caller's.
    template <typename OnToggle>
    void applyCheckboxClick(entt::registry& registry, entt::entity checkbox, OnToggle&& onToggle)
    {
        auto& comp = registry.get<components::UICheckboxComponent>(checkbox);
        const bool previousChecked = comp.isChecked;
        bool toggled = false;

        if (comp.groupName.empty())
        {
            // Independent checkbox: simply toggle
            comp.isChecked = !comp.isChecked;
            toggled = (comp.isChecked != previousChecked);
        }
        else if (comp.isChecked && !comp.allowUncheck)
        {
            // Already checked, can't uncheck in strict radio mode
        }
        else if (!comp.isChecked)
        {
            // Check this one, uncheck all others in the same group
            comp.isChecked = true;
            toggled = true;

            // Copied: a listener may touch the registry while the peers are notified.
            const std::string groupName = comp.groupName;
            auto checkboxView = registry.view<components::UICheckboxComponent, components::UIRectComponent>();
            for (auto otherEntity : checkboxView)
            {
                if (otherEntity == checkbox)
                    continue;
                auto& otherComp = registry.get<components::UICheckboxComponent>(otherEntity);
                if (otherComp.groupName == groupName && otherComp.isChecked)
                {
                    otherComp.isChecked = false;
                    onToggle(otherEntity, false, true);
                }
            }
        }
        else
        {
            // Radio group but allowUncheck is true: toggle off
            comp.isChecked = false;
            toggled = true;
        }

        if (toggled)
        {
            // Re-fetched: the peer notifications above may have reallocated the component storage.
            if (const auto* now = registry.try_get<components::UICheckboxComponent>(checkbox))
                onToggle(checkbox, now->isChecked, previousChecked);
        }
    }
}
