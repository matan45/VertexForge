#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <string>

namespace events::ui {

    // ============================================
    // UI ListView Commands
    // ============================================

    struct AddUIListViewComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIListViewComponent"; }
    };

    struct RemoveUIListViewComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIListViewComponent"; }
    };

    struct SetUIListViewDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIListViewData listViewData;

        std::string_view getName() const override { return "SetUIListViewData"; }
    };

    // Sets the bound item count and reconciles instances (pool-first growth).
    struct SetUIListItemCountCommand : ICommand<bool> {
        services::EntityHandle entity;
        int itemCount = 0;

        std::string_view getName() const override { return "SetUIListItemCount"; }
    };

    // Assigns the .vfPrefab item template by path and rebuilds all instances.
    struct SetUIListItemTemplateCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string templatePath;

        std::string_view getName() const override { return "SetUIListItemTemplate"; }
    };

    struct SetUIListSelectedIndexCommand : ICommand<bool> {
        services::EntityHandle entity;
        int selectedIndex = -1;

        std::string_view getName() const override { return "SetUIListSelectedIndex"; }
    };

    // ============================================
    // UI ListView Queries
    // ============================================

    struct HasUIListViewComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIListViewComponent"; }
    };

    struct GetUIListViewDataQuery : IQuery<std::optional<services::UIListViewData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIListViewData"; }
    };

    // Root entity of the item instance at `index` (invalid handle on miss).
    struct GetUIListItemQuery : IQuery<services::EntityHandle> {
        services::EntityHandle entity;
        int index = -1;

        std::string_view getName() const override { return "GetUIListItem"; }
    };

    // ============================================
    // UI ListView Notifications
    // ============================================

    struct UIListSelectionChangedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        int previousIndex = -1;
        int newIndex = -1;

        std::string_view getName() const override { return "UIListSelectionChanged"; }
    };

}
