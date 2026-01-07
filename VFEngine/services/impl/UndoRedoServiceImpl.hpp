#pragma once
#include "../interfaces/IUndoRedoService.hpp"
#include "../events/UndoRedoEvents.hpp"
#include "../events/EventDispatcher.hpp"
#include <vector>
#include <memory>

namespace services
{
    class UndoRedoServiceImpl : public IUndoRedoService
    {
    public:
        UndoRedoServiceImpl();
        ~UndoRedoServiceImpl() override = default;

        void registerEventHandlers() override;

        // Command History Management
        void pushCommand(std::unique_ptr<IUndoableCommand> command) override;
        bool undo() override;
        bool redo() override;
        void clear() override;

        // State Queries
        bool canUndo() const override;
        bool canRedo() const override;
        std::string getUndoDescription() const override;
        std::string getRedoDescription() const override;
        size_t getUndoStackSize() const override;
        size_t getRedoStackSize() const override;

        // Configuration
        void setMaxHistoryDepth(size_t maxDepth) override;
        size_t getMaxHistoryDepth() const override;

    private:
        void publishStateChanged();
        void trimUndoStack();

        std::vector<std::unique_ptr<IUndoableCommand>> undoStack;
        std::vector<std::unique_ptr<IUndoableCommand>> redoStack;
        size_t maxHistoryDepth = 50;  // Default to 50 commands
    };
}
