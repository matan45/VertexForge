#pragma once
#include "../EventTypes.hpp"
#include "config/EditorPreferences.hpp"

namespace events::editor {

    // ============================================
    // COMMANDS - Operations that modify editor settings
    // ============================================

    struct SetEditorSettingsCommand : ICommand<bool> {
        config::EditorPreferences settings;

        std::string_view getName() const override { return "SetEditorSettings"; }
    };

    struct SaveEditorSettingsCommand : ICommand<bool> {
        std::string_view getName() const override { return "SaveEditorSettings"; }
    };

    struct ResetEditorSettingsCommand : ICommand<bool> {
        std::string_view getName() const override { return "ResetEditorSettings"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    struct GetEditorSettingsQuery : IQuery<config::EditorPreferences> {
        std::string_view getName() const override { return "GetEditorSettings"; }
    };

    // Absolute path to the editor preferences JSON file on disk.
    struct GetEditorSettingsPathQuery : IQuery<std::string> {
        std::string_view getName() const override { return "GetEditorSettingsPath"; }
    };

    // ============================================
    // NOTIFICATIONS - State change broadcasts
    // ============================================

    struct EditorSettingsChangedNotification : INotification {
        config::EditorPreferences settings;

        std::string_view getName() const override { return "EditorSettingsChanged"; }
    };

}
