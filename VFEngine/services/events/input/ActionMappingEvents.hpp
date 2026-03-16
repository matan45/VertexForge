#pragma once
#include "../EventTypes.hpp"
#include "../../data/ActionMappingTypes.hpp"
#include <string>
#include <vector>

namespace events::input {

    // ============================================
    // QUERIES - Action state read-only operations
    // ============================================

    struct IsActionDownQuery : IQuery<bool> {
        std::string actionName;

        std::string_view getName() const override { return "IsActionDown"; }
    };

    struct IsActionPressedQuery : IQuery<bool> {
        std::string actionName;

        std::string_view getName() const override { return "IsActionPressed"; }
    };

    struct IsActionReleasedQuery : IQuery<bool> {
        std::string actionName;

        std::string_view getName() const override { return "IsActionReleased"; }
    };

    struct GetActionBindingsQuery : IQuery<std::vector<services::InputBinding>> {
        std::string actionName;

        std::string_view getName() const override { return "GetActionBindings"; }
    };

    struct GetAllActionNamesQuery : IQuery<std::vector<std::string>> {
        std::string_view getName() const override { return "GetAllActionNames"; }
    };

    // ============================================
    // COMMANDS - Action mapping mutations
    // ============================================

    struct RegisterActionCommand : ICommand<void> {
        std::string actionName;
        std::vector<services::InputBinding> defaultBindings;

        std::string_view getName() const override { return "RegisterAction"; }
    };

    struct AddActionBindingCommand : ICommand<void> {
        std::string actionName;
        services::InputBinding binding;

        std::string_view getName() const override { return "AddActionBinding"; }
    };

    struct RemoveActionBindingCommand : ICommand<void> {
        std::string actionName;
        services::InputBinding binding;

        std::string_view getName() const override { return "RemoveActionBinding"; }
    };

    struct SetActionBindingsCommand : ICommand<void> {
        std::string actionName;
        std::vector<services::InputBinding> bindings;

        std::string_view getName() const override { return "SetActionBindings"; }
    };

    struct ResetActionBindingsCommand : ICommand<void> {
        std::string actionName;

        std::string_view getName() const override { return "ResetActionBindings"; }
    };

    struct ResetAllActionBindingsCommand : ICommand<void> {
        std::string_view getName() const override { return "ResetAllActionBindings"; }
    };

    struct SaveActionBindingsCommand : ICommand<bool> {
        std::string filePath;

        std::string_view getName() const override { return "SaveActionBindings"; }
    };

    struct LoadActionBindingsCommand : ICommand<bool> {
        std::string filePath;

        std::string_view getName() const override { return "LoadActionBindings"; }
    };

}
