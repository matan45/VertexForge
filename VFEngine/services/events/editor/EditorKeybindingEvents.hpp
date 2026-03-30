#pragma once
#include "../EventTypes.hpp"
#include "../../data/EditorKeybindingTypes.hpp"

namespace events::editor {

    // ============================================
    // COMMANDS
    // ============================================

    struct RegisterEditorActionCommand : ICommand<> {
        std::string actionName;
        std::string category;
        std::string displayName;
        std::vector<services::InputBinding> defaultBindings;

        std::string_view getName() const override { return "RegisterEditorAction"; }
    };

    struct SetEditorActionBindingsCommand : ICommand<> {
        std::string actionName;
        std::vector<services::InputBinding> bindings;

        std::string_view getName() const override { return "SetEditorActionBindings"; }
    };

    struct ResetEditorActionBindingsCommand : ICommand<> {
        std::string actionName; // empty = reset all

        std::string_view getName() const override { return "ResetEditorActionBindings"; }
    };

    struct SaveEditorKeybindingsCommand : ICommand<bool> {
        std::string_view getName() const override { return "SaveEditorKeybindings"; }
    };

    // ============================================
    // QUERIES
    // ============================================

    struct GetEditorActionBindingsQuery : IQuery<std::vector<services::InputBinding>> {
        std::string actionName;

        std::string_view getName() const override { return "GetEditorActionBindings"; }
    };

    struct GetAllEditorActionsQuery : IQuery<std::vector<services::EditorActionInfo>> {
        std::string_view getName() const override { return "GetAllEditorActions"; }
    };

    struct IsEditorActionPressedQuery : IQuery<bool> {
        std::string actionName;

        std::string_view getName() const override { return "IsEditorActionPressed"; }
    };

    struct GetKeybindingConflictsQuery : IQuery<std::vector<services::KeybindingConflict>> {
        std::string actionName;
        services::InputBinding binding;

        std::string_view getName() const override { return "GetKeybindingConflicts"; }
    };

}
