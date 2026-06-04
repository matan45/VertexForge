#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <string>

namespace events::ui {

    // ============================================
    // UI TextInput Commands
    // ============================================

    struct AddUITextInputComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUITextInputComponent"; }
    };

    struct RemoveUITextInputComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUITextInputComponent"; }
    };

    struct SetUITextInputDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UITextInputData textInputData;

        std::string_view getName() const override { return "SetUITextInputData"; }
    };

    // ============================================
    // UI TextInput Queries
    // ============================================

    struct HasUITextInputComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUITextInputComponent"; }
    };

    struct GetUITextInputDataQuery : IQuery<std::optional<services::UITextInputData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUITextInputData"; }
    };

    // ============================================
    // UI TextInput Notifications
    // ============================================

    struct UITextInputSubmitNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        std::string text;

        std::string_view getName() const override { return "UITextInputSubmit"; }
    };

    struct UITextInputChangedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        std::string text;

        std::string_view getName() const override { return "UITextInputChanged"; }
    };

    struct UITextInputFocusedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UITextInputFocused"; }
    };

    struct UITextInputUnfocusedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UITextInputUnfocused"; }
    };

    // ============================================
    // UI Checkbox Commands
    // ============================================

    struct AddUICheckboxComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUICheckboxComponent"; }
    };

    struct RemoveUICheckboxComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUICheckboxComponent"; }
    };

    struct SetUICheckboxDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UICheckboxData checkboxData;

        std::string_view getName() const override { return "SetUICheckboxData"; }
    };

    // ============================================
    // UI Checkbox Queries
    // ============================================

    struct HasUICheckboxComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUICheckboxComponent"; }
    };

    struct GetUICheckboxDataQuery : IQuery<std::optional<services::UICheckboxData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUICheckboxData"; }
    };

    // ============================================
    // UI Checkbox Notifications
    // ============================================

    struct UICheckboxToggledNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        bool newCheckedState;
        bool previousCheckedState;

        std::string_view getName() const override { return "UICheckboxToggled"; }
    };

    struct UICheckboxHoverEnterNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UICheckboxHoverEnter"; }
    };

    struct UICheckboxHoverExitNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UICheckboxHoverExit"; }
    };

}
