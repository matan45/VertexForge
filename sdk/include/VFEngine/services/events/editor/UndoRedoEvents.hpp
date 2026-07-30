#pragma once
#include "../EventTypes.hpp"
#include "../../data/UndoTypes.hpp"
#include <string>
#include <memory>

namespace events::undoredo {

    struct UndoCommand : ICommand<bool> {
        std::string_view getName() const override { return "Undo"; }
    };

    // Push an arbitrary undoable command onto the stack. Lets services that
    // don't hold the undo service (e.g. the vegetation brush) record undo
    // entries via the event dispatcher.
    struct PushUndoableCommand : ICommand<> {
        std::shared_ptr<services::IUndoableCommand> command;
        std::string_view getName() const override { return "PushUndoableCommand"; }
    };

    struct RedoCommand : ICommand<bool> {
        std::string_view getName() const override { return "Redo"; }
    };

    struct BeginBatchCommand : ICommand<> {
        std::string description;
        std::string_view getName() const override { return "BeginBatch"; }
    };

    struct EndBatchCommand : ICommand<> {
        std::string_view getName() const override { return "EndBatch"; }
    };

    // Applies the persisted undo-history limits. 0 == unlimited for either field.
    // maxBytes exists because a depth cap cannot bound RAM: a terrain paint stroke
    // snapshots whole tile weight maps (~532 KB per 129^2 tile, before AND after).
    struct SetUndoHistoryLimitsCommand : ICommand<> {
        size_t maxDepth = 50;
        size_t maxBytes = 0;
        std::string_view getName() const override { return "SetUndoHistoryLimits"; }
    };

    struct GetUndoHistoryStatsQuery : IQuery<services::UndoHistoryStats> {
        std::string_view getName() const override { return "GetUndoHistoryStats"; }
    };

}
