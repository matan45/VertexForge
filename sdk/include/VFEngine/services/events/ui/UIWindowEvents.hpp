#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <string>

namespace events::ui {

    // ============================================
    // UI Window Commands
    // ============================================

    struct AddUIWindowComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIWindowComponent"; }
    };

    struct RemoveUIWindowComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIWindowComponent"; }
    };

    struct SetUIWindowDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIWindowData windowData;

        std::string_view getName() const override { return "SetUIWindowData"; }
    };

    // Activates the window entity; modal windows also push onto the modal stack.
    struct OpenUIWindowCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "OpenUIWindow"; }
    };

    // Deactivates the window entity, pops it from the modal stack and
    // publishes UIWindowClosedNotification.
    struct CloseUIWindowCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "CloseUIWindow"; }
    };

    // ============================================
    // UI Window Queries
    // ============================================

    struct HasUIWindowComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIWindowComponent"; }
    };

    struct GetUIWindowDataQuery : IQuery<std::optional<services::UIWindowData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIWindowData"; }
    };

    struct IsUIWindowOpenQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsUIWindowOpen"; }
    };

    // ============================================
    // UI Window Notifications
    // ============================================

    struct UIWindowOpenedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIWindowOpened"; }
    };

    struct UIWindowClosedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIWindowClosed"; }
    };

}
