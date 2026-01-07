#pragma once
#include "EventTypes.hpp"
#include <string>

namespace events::undoredo {

    // ============================================
    // COMMANDS - Undo/Redo operations
    // ============================================

    struct UndoCommand : ICommand<bool> {
        std::string_view getName() const override { return "Undo"; }
    };

    struct RedoCommand : ICommand<bool> {
        std::string_view getName() const override { return "Redo"; }
    };

    struct ClearHistoryCommand : ICommand<> {
        std::string_view getName() const override { return "ClearHistory"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    struct CanUndoQuery : IQuery<bool> {
        std::string_view getName() const override { return "CanUndo"; }
    };

    struct CanRedoQuery : IQuery<bool> {
        std::string_view getName() const override { return "CanRedo"; }
    };

    struct GetUndoDescriptionQuery : IQuery<std::string> {
        std::string_view getName() const override { return "GetUndoDescription"; }
    };

    struct GetRedoDescriptionQuery : IQuery<std::string> {
        std::string_view getName() const override { return "GetRedoDescription"; }
    };

    // ============================================
    // NOTIFICATIONS - Broadcast state changes
    // ============================================

    struct UndoStateChangedNotification : INotification {
        bool canUndo = false;
        bool canRedo = false;
        std::string undoDescription;
        std::string redoDescription;

        std::string_view getName() const override { return "UndoStateChanged"; }
    };

    struct UndoPerformedNotification : INotification {
        std::string description;

        std::string_view getName() const override { return "UndoPerformed"; }
    };

    struct RedoPerformedNotification : INotification {
        std::string description;

        std::string_view getName() const override { return "RedoPerformed"; }
    };

}
