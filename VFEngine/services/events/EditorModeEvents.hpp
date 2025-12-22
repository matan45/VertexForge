#pragma once
#include "EventTypes.hpp"
#include "../data/EditorMode.hpp"

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
    // NOTIFICATIONS - State change broadcasts
    // ============================================

    struct EditorModeChangedNotification : INotification {
        services::EditorMode previousMode;
        services::EditorMode currentMode;

        std::string_view getName() const override { return "EditorModeChanged"; }
    };

}
