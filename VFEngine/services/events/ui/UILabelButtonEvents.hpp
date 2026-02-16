#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <string>

namespace events::ui {

    // ============================================
    // UI Label Commands
    // ============================================

    struct AddUILabelComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUILabelComponent"; }
    };

    struct RemoveUILabelComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUILabelComponent"; }
    };

    struct SetUILabelDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UILabelData labelData;

        std::string_view getName() const override { return "SetUILabelData"; }
    };

    // ============================================
    // UI Label Queries
    // ============================================

    struct HasUILabelComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUILabelComponent"; }
    };

    struct GetUILabelDataQuery : IQuery<std::optional<services::UILabelData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUILabelData"; }
    };

    struct GetUILabelPreferredSizeQuery : IQuery<std::optional<glm::vec2>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUILabelPreferredSize"; }
    };

    // ============================================
    // UI Button Commands
    // ============================================

    struct AddUIButtonComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIButtonComponent"; }
    };

    struct RemoveUIButtonComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIButtonComponent"; }
    };

    struct SetUIButtonDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIButtonData buttonData;

        std::string_view getName() const override { return "SetUIButtonData"; }
    };

    // ============================================
    // UI Button Queries
    // ============================================

    struct HasUIButtonComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIButtonComponent"; }
    };

    struct GetUIButtonDataQuery : IQuery<std::optional<services::UIButtonData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIButtonData"; }
    };

    // ============================================
    // UI Button Notifications
    // ============================================

    struct UIButtonClickedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIButtonClicked"; }
    };

    struct UIButtonPressedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIButtonPressed"; }
    };

    struct UIButtonReleasedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIButtonReleased"; }
    };

    struct UIButtonHoverEnterNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIButtonHoverEnter"; }
    };

    struct UIButtonHoverExitNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIButtonHoverExit"; }
    };

}
