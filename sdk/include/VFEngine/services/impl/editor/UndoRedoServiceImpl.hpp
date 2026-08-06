#pragma once
#include "../../interfaces/editor/IUndoRedoService.hpp"
#include "../../events/EventDispatcher.hpp"
#include <vector>
#include <memory>

namespace services
{
    class UndoRedoServiceImpl : public IUndoRedoService
    {
    private:
        std::vector<std::unique_ptr<IUndoableCommand>> undoStack;
        std::vector<std::unique_ptr<IUndoableCommand>> redoStack;
        size_t maxHistoryDepth = 50; // Default to 50 commands; 0 = unlimited

        // Byte ceiling across BOTH stacks. 0 = unlimited. Tracked incrementally because
        // getMemoryFootprint() walks heap containers and the stacks are touched every
        // push/undo/redo; recomputing the sum each time would be O(stack) per keystroke.
        size_t maxHistoryBytes = 0;
        size_t currentBytes = 0;

        bool inBatchMode = false;
        std::string batchDescription;
        std::unique_ptr<BatchUndoCommand> currentBatch;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken settingsChangedToken;

    public:
        explicit UndoRedoServiceImpl();
        ~UndoRedoServiceImpl() override;

        void registerEventHandlers() override;
        
        void pushCommand(std::unique_ptr<IUndoableCommand> command) override;
        bool undo() override;
        bool redo() override;
        void clear() override;
        
        bool canUndo() const override;
        bool canRedo() const override;
        std::string getUndoDescription() const override;
        std::string getRedoDescription() const override;
        
        void beginBatch(const std::string& description) override;
        void endBatch() override;

        void setHistoryLimits(size_t maxDepth, size_t maxBytes) override;
        UndoHistoryStats getHistoryStats() const override;

    private:
        void trimUndoStack();
        void trimRedoStack();

        // Drops every entry in `stack`, subtracting its bytes from currentBytes.
        void clearStackBytes(std::vector<std::unique_ptr<IUndoableCommand>>& stack);
    };
}
