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
        size_t maxHistoryDepth = 50; // Default to 50 commands
        
        bool inBatchMode = false;
        std::string batchDescription;
        std::unique_ptr<BatchUndoCommand> currentBatch;

    public:
        explicit UndoRedoServiceImpl();
        ~UndoRedoServiceImpl() override = default;

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

    private:
        void trimUndoStack();
    };
}
