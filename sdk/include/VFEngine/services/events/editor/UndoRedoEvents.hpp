#pragma once
#include "../EventTypes.hpp"
#include <string>

namespace events::undoredo {

    struct UndoCommand : ICommand<bool> {
        std::string_view getName() const override { return "Undo"; }
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

}
