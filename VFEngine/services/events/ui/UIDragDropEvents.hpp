#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <string>

namespace events::ui {

    // ============================================
    // UI Draggable Commands
    // ============================================

    struct AddUIDraggableComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIDraggableComponent"; }
    };

    struct RemoveUIDraggableComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIDraggableComponent"; }
    };

    struct SetUIDraggableDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIDraggableData draggableData;

        std::string_view getName() const override { return "SetUIDraggableData"; }
    };

    // ============================================
    // UI DropTarget Commands
    // ============================================

    struct AddUIDropTargetComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIDropTargetComponent"; }
    };

    struct RemoveUIDropTargetComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIDropTargetComponent"; }
    };

    struct SetUIDropTargetDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIDropTargetData dropTargetData;

        std::string_view getName() const override { return "SetUIDropTargetData"; }
    };

    // ============================================
    // UI Draggable Queries
    // ============================================

    struct HasUIDraggableComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIDraggableComponent"; }
    };

    struct GetUIDraggableDataQuery : IQuery<std::optional<services::UIDraggableData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIDraggableData"; }
    };

    // ============================================
    // UI DropTarget Queries
    // ============================================

    struct HasUIDropTargetComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIDropTargetComponent"; }
    };

    struct GetUIDropTargetDataQuery : IQuery<std::optional<services::UIDropTargetData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIDropTargetData"; }
    };

    // ============================================
    // UI Drag & Drop Runtime Commands
    // ============================================

    struct CancelDragCommand : ICommand<bool> {
        std::string_view getName() const override { return "CancelDrag"; }
    };

    // ============================================
    // UI Drag & Drop Notifications
    // ============================================

    struct UIDragStartNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        std::string dragTag;

        std::string_view getName() const override { return "UIDragStart"; }
    };

    struct UIDragEndNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        bool wasDropped;

        std::string_view getName() const override { return "UIDragEnd"; }
    };

    struct UIDropNotification : INotification {
        services::EntityHandle sourceEntity;
        std::string sourceEntityName;
        services::EntityHandle targetEntity;
        std::string targetEntityName;
        std::string dragTag;

        std::string_view getName() const override { return "UIDrop"; }
    };

}
