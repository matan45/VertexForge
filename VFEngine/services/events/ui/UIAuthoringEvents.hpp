#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/UIAuthoringData.hpp"
#include <string>

// VK-1659 UI authoring events (MCP P10), handled by UIComponentService
// (services/impl/components/UIAuthoringOps.cpp). Main thread, like every UI command.

namespace events::ui {

    // Clicks a UIButton or UICheckbox the way a left click over it does, after the same checks the runtime hit
    // test makes (UIInteractionSystem findHoveredWidget): a UIRect, interactable, effectively active, a canvas,
    // not blocked by a modal window. The UILabel child of a labelToggle checkbox clicks that checkbox.
    //   Button:   publishes UIButtonPressed -> UIButtonReleased -> UIButtonClicked (no hover events).
    //   Checkbox: applies the runtime toggle rule (utilities/ui/UICheckboxToggle.hpp, radio groups included)
    //             and publishes UICheckboxToggled for every box it changed.
    // Listener scripts (IUIButtonListener, IUICheckboxListener) run synchronously inside the call. Callers
    // check Play mode: script instances exist only in Play.
    struct SimulateUIClickCommand : ICommand<services::UIClickResult> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "SimulateUIClick"; }
    };

    // Rebuilds the item instances of every UIListView in `root`'s subtree (root included) from its template and
    // item count. Item instances are never serialized, so anything that instantiates entities from JSON or a
    // prefab runs this on the new root (ScenePersistenceService::loadPrefab / instantiateEntityFromJson).
    // Returns how many list views were reconciled.
    struct ReconcileUIListViewsCommand : ICommand<int> {
        services::EntityHandle root;

        std::string_view getName() const override { return "ReconcileUIListViews"; }
    };

    // True when `entity` is an engine-managed UIListView item instance (UIListItemComponent): rebuilt from the
    // template, never serialized, so never edited directly.
    struct IsUIListItemQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsUIListItem"; }
    };

    // The structure checks of a compound widget (tabs, dropdown, tooltip, checkbox).
    struct GetUICompoundStatusQuery : IQuery<services::UICompoundStatusData> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUICompoundStatus"; }
    };

    // True when `entity` lies in a UI Layer Builder preview sandbox: it or an ancestor carries
    // UIPreviewTagComponent (MarkUIPreviewSandboxCommand). The sandbox is the builder's editor-only working copy,
    // never drawn in the scene nor saved with it.
    struct IsInUIPreviewSandboxQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsInUIPreviewSandbox"; }
    };

}
