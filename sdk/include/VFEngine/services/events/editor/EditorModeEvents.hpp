#pragma once
#include "../EventTypes.hpp"
#include "../../data/EditorMode.hpp"

namespace events::editor {

    // ============================================
    // COMMANDS - Operations that modify editor mode state
    // ============================================

    struct SetEditorModeCommand : ICommand<> {
        services::EditorMode mode;

        std::string_view getName() const override { return "SetEditorMode"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    struct GetEditorModeQuery : IQuery<services::EditorMode> {
        std::string_view getName() const override { return "GetEditorMode"; }
    };

    struct IsPlayModeQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsPlayMode"; }
    };

    struct IsEditModeQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsEditMode"; }
    };

    // ============================================
    // PAUSE COMMANDS
    // ============================================

    struct SetEditorPausedCommand : ICommand<> {
        bool paused;

        std::string_view getName() const override { return "SetEditorPaused"; }
    };

    // ============================================
    // PAUSE QUERIES
    // ============================================

    struct IsEditorPausedQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsEditorPaused"; }
    };

    // ============================================
    // NOTIFICATIONS - State change broadcasts
    // ============================================

    struct EditorModePreChangeNotification : INotification {
        services::EditorMode previousMode;
        services::EditorMode currentMode;

        std::string_view getName() const override { return "EditorModePreChange"; }
    };

    struct EditorModeChangedNotification : INotification {
        services::EditorMode previousMode;
        services::EditorMode currentMode;

        std::string_view getName() const override { return "EditorModeChanged"; }
    };

    struct EditorPauseChangedNotification : INotification {
        bool paused;

        std::string_view getName() const override { return "EditorPauseChanged"; }
    };

}
