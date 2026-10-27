#pragma once
#include "EntityHandle.hpp"
#include <cstdint>
#include <string>
#include <vector>

// VK-1659 UI authoring DTOs (MCP P10): the answers of the events in events/ui/UIAuthoringEvents.hpp.
// New types only, so no existing (plugin-visible) layout changes.

namespace services
{
    // SimulateUIClickCommand's verdict. Every status but Clicked means nothing was changed or published.
    enum class UIClickStatus : uint8_t
    {
        Clicked = 0,
        NotFound,         // the entity does not exist
        NotClickable,     // neither a UIButton nor a UICheckbox, nor the UILabel child of a labelToggle checkbox
        NoRect,           // the widget has no UIRect: the runtime never hit-tests it
        NotInteractable,  // interactable is false
        Inactive,         // not effectively active (it or an ancestor is inactive)
        NoCanvas,         // no UICanvas on it or an ancestor: the runtime never hit-tests it
        BlockedByModal    // an open modal window outside its subtree blocks it (UIRectMath isInteractionAllowed)
    };

    enum class UIClickKind : uint8_t
    {
        None = 0,
        Button,
        Checkbox
    };

    // One UICheckboxToggledNotification the click published.
    struct UICheckboxToggleData
    {
        EntityHandle entity;
        std::string name;
        bool newChecked = false;
        bool previousChecked = false;
    };

    struct UIClickResult
    {
        UIClickStatus status = UIClickStatus::NotFound;
        UIClickKind kind = UIClickKind::None;
        // The widget that took the click: the entity itself, or the checkbox whose labelToggle label was clicked.
        EntityHandle target;
        std::string targetName;
        // Checkbox: every toggle, in publish order (radio peers first, the clicked box last). Empty for a button,
        // and for a strict radio box that was already checked.
        std::vector<UICheckboxToggleData> toggles;
    };

    // ui_validation::validateCompoundWidget (utilities/ui/UICompoundValidation.hpp): the structure checks of a
    // tabs, dropdown, tooltip or checkbox widget. ok and both lists empty for any other entity.
    struct UICompoundStatusData
    {
        bool ok = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
}
