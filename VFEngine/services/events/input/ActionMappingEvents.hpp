#pragma once
#include "../EventTypes.hpp"
#include "../../data/ActionMappingTypes.hpp"
#include <glm/glm.hpp>
#include <optional>
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

    struct UnregisterActionCommand : ICommand<void> {
        std::string actionName;

        std::string_view getName() const override { return "UnregisterAction"; }
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

    // ============================================
    // QUERIES - Axis value read-only operations
    // ============================================

    struct GetAxis1DValueQuery : IQuery<float> {
        std::string axisName;

        std::string_view getName() const override { return "GetAxis1DValue"; }
    };

    struct GetAxis2DValueQuery : IQuery<glm::vec2> {
        std::string axisName;

        std::string_view getName() const override { return "GetAxis2DValue"; }
    };

    struct GetAllAxis1DNamesQuery : IQuery<std::vector<std::string>> {
        std::string_view getName() const override { return "GetAllAxis1DNames"; }
    };

    struct GetAllAxis2DNamesQuery : IQuery<std::vector<std::string>> {
        std::string_view getName() const override { return "GetAllAxis2DNames"; }
    };

    struct GetAxis1DDefinitionQuery : IQuery<std::optional<services::Axis1DDefinition>> {
        std::string axisName;

        std::string_view getName() const override { return "GetAxis1DDefinition"; }
    };

    struct GetAxis2DDefinitionQuery : IQuery<std::optional<services::Axis2DDefinition>> {
        std::string axisName;

        std::string_view getName() const override { return "GetAxis2DDefinition"; }
    };

    // ============================================
    // COMMANDS - Axis registration mutations
    // ============================================

    struct RegisterAxis1DCommand : ICommand<void> {
        std::string axisName;
        std::string positiveAction;
        std::string negativeAction;

        std::string_view getName() const override { return "RegisterAxis1D"; }
    };

    struct RegisterAxis2DCommand : ICommand<void> {
        std::string axisName;
        std::string upAction;
        std::string downAction;
        std::string leftAction;
        std::string rightAction;
        bool normalize = true;

        std::string_view getName() const override { return "RegisterAxis2D"; }
    };

    struct UnregisterAxis1DCommand : ICommand<void> {
        std::string axisName;

        std::string_view getName() const override { return "UnregisterAxis1D"; }
    };

    struct UnregisterAxis2DCommand : ICommand<void> {
        std::string axisName;

        std::string_view getName() const override { return "UnregisterAxis2D"; }
    };

}
