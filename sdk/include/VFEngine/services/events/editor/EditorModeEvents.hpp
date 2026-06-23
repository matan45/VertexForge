#pragma once
#include "../EventTypes.hpp"
#include "../../data/EditorMode.hpp"

namespace events::editor {

    // ============================================
    // COMMANDS - Operations that modify editor mode state
    // ============================================

    struct SetEditorModeCommand : ICommand<> {
        services::EditorMode mode;
        // VK-1371: when entering Play, also start the embedded mType debug server
        // so VS Code can attach and debug the running scripts.
        bool withDebugger = false;

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
    // TIME CONTROL (Phase 2) - step + time scale
    // ============================================

    // Advance exactly one gameplay frame while paused.
    struct StepFrameCommand : ICommand<> {
        std::string_view getName() const override { return "StepFrame"; }
    };

    // Set the gameplay time scale (slow-mo / fast-forward). Clamped service-side.
    struct SetTimeScaleCommand : ICommand<> {
        float scale = 1.0f;

        std::string_view getName() const override { return "SetTimeScale"; }
    };

    struct GetTimeScaleQuery : IQuery<float> {
        std::string_view getName() const override { return "GetTimeScale"; }
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

    struct TimeScaleChangedNotification : INotification {
        float scale = 1.0f;

        std::string_view getName() const override { return "TimeScaleChanged"; }
    };

}
