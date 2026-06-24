#pragma once

// VK-1435 — generic inspector-edit undo for the UI Layer Builder.
//
// The inspector embeds the existing UI*Drawer set; each drawer edits a UI component's data
// live through the Set*DataCommand CQRS. To make EVERY inspector field undoable (not just the
// UIRect), we snapshot the selected entity's editable UI component data before an edit session
// and, on edit-deactivation, push ONE IUndoableCommand holding the before/after snapshots. The
// command replays the matching Set*DataCommand for each present component on undo()/execute().
//
// Identity-preserving: we restore typed component DATA onto the SAME entity (no re-instantiation,
// which would mint a new entity/UUID and break the selection + on-canvas handles). Stays entirely
// editor-side + entt-free — all through the existing services::events::ui CQRS.

#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "data/UndoTypes.hpp"
#include <optional>
#include <string>
#include <memory>

namespace windows::uilayer
{
    // A point-in-time snapshot of all editable UI component data on one entity. Each optional
    // is engaged only if the entity carries that component (so applying restores exactly the
    // set of components that existed — the inspector never adds/removes components mid-edit).
    struct UIComponentSnapshot
    {
        std::optional<services::UICanvasData> canvas;
        std::optional<services::UIRectData> rect;
        std::optional<services::UIImageData> image;
        std::optional<services::UILabelData> label;
        std::optional<services::UIScrollData> scroll;
        std::optional<services::UILayoutGroupData> layoutGroup;
        std::optional<services::UIButtonData> button;
        std::optional<services::UITextInputData> textInput;
        std::optional<services::UICheckboxData> checkbox;
        std::optional<services::UIDropdownData> dropdown;
        std::optional<services::UITabsData> tabs;
        std::optional<services::UISliderData> slider;
        std::optional<services::UIProgressBarData> progressBar;
        std::optional<std::string> styleKey;
        std::optional<services::UIAnimationData> animation;
        std::optional<services::UIListViewData> listView;
        std::optional<services::UIWindowData> window;
        std::optional<services::UITooltipData> tooltip;
        std::optional<services::UIMaskData> mask;
        std::optional<services::UIDraggableData> draggable;
        std::optional<services::UIDropTargetData> dropTarget;
    };

    // Capture the editable UI component data currently on `entity` (via Get*DataQuery / the
    // UIStyle-key query). Components the entity lacks stay nullopt.
    UIComponentSnapshot captureUISnapshot(services::EntityHandle entity);

    // Apply a snapshot back onto `entity` (via Set*DataCommand / SetUIStyleKeyCommand) for every
    // engaged field. Restores in place; the entity must still exist (no-op on stale handles).
    void applyUISnapshot(services::EntityHandle entity, const UIComponentSnapshot& snapshot);

    // Undoable command: restores `before` on undo(), `after` on execute()/redo().
    class UIComponentEditUndoCommand : public services::IUndoableCommand
    {
    public:
        UIComponentEditUndoCommand(services::EntityHandle entity,
                                   UIComponentSnapshot before, UIComponentSnapshot after,
                                   std::string description);

        void execute() override; // redo
        void undo() override;
        std::string getDescription() const override { return description; }

    private:
        services::EntityHandle entity;
        UIComponentSnapshot before;
        UIComponentSnapshot after;
        std::string description;
    };
}
