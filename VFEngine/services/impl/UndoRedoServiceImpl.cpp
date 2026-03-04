#include "UndoRedoServiceImpl.hpp"
#include "../events/UndoRedoEvents.hpp"

namespace services
{
    UndoRedoServiceImpl::UndoRedoServiceImpl()
    {
    }

    void UndoRedoServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::undoredo::UndoCommand>(
            [this](const events::undoredo::UndoCommand&)
            {
                return undo();
            });

        dispatcher.registerCommandHandler<events::undoredo::RedoCommand>(
            [this](const events::undoredo::RedoCommand&)
            {
                return redo();
            });

        dispatcher.registerCommandHandler<events::undoredo::BeginBatchCommand>(
            [this](const events::undoredo::BeginBatchCommand& cmd)
            {
                beginBatch(cmd.description);
            });

        dispatcher.registerCommandHandler<events::undoredo::EndBatchCommand>(
            [this](const events::undoredo::EndBatchCommand&)
            {
                endBatch();
            });
    }

    void UndoRedoServiceImpl::pushCommand(std::unique_ptr<IUndoableCommand> command)
    {
        if (!command)
        {
            return;
        }

        if (inBatchMode && currentBatch)
        {
            currentBatch->addCommand(std::move(command));
            return;
        }

        redoStack.clear();

        undoStack.push_back(std::move(command));

        trimUndoStack();

    }

    bool UndoRedoServiceImpl::undo()
    {
        if (!canUndo())
        {
            return false;
        }

        auto command = std::move(undoStack.back());
        undoStack.pop_back();

        std::string description = command->getDescription();

        try
        {
            command->undo();

            redoStack.push_back(std::move(command));

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Undo failed: {}", e.what());
            undoStack.push_back(std::move(command));
            return false;
        }
    }

    bool UndoRedoServiceImpl::redo()
    {
        if (!canRedo())
        {
            return false;
        }

        auto command = std::move(redoStack.back());
        redoStack.pop_back();

        std::string description = command->getDescription();

        try
        {
            command->execute();

            undoStack.push_back(std::move(command));

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Redo failed: {}", e.what());
            redoStack.push_back(std::move(command));
            return false;
        }
    }

    void UndoRedoServiceImpl::clear()
    {
        undoStack.clear();
        redoStack.clear();
    }

    bool UndoRedoServiceImpl::canUndo() const
    {
        return !undoStack.empty();
    }

    bool UndoRedoServiceImpl::canRedo() const
    {
        return !redoStack.empty();
    }

    std::string UndoRedoServiceImpl::getUndoDescription() const
    {
        if (undoStack.empty())
        {
            return "";
        }
        return undoStack.back()->getDescription();
    }

    std::string UndoRedoServiceImpl::getRedoDescription() const
    {
        if (redoStack.empty())
        {
            return "";
        }
        return redoStack.back()->getDescription();
    }

    void UndoRedoServiceImpl::trimUndoStack()
    {
        if (maxHistoryDepth == 0)
        {
            return;
        }

        while (undoStack.size() > maxHistoryDepth)
        {
            undoStack.erase(undoStack.begin());
        }
    }

    void UndoRedoServiceImpl::beginBatch(const std::string& description)
    {
        if (inBatchMode)
        {
            vfLogWarning("Already in batch mode, ignoring beginBatch call");
            return;
        }

        inBatchMode = true;
        batchDescription = description;
        currentBatch = std::make_unique<BatchUndoCommand>(description);
    }

    void UndoRedoServiceImpl::endBatch()
    {
        if (!inBatchMode)
        {
            vfLogWarning("Not in batch mode, ignoring endBatch call");
            return;
        }

        inBatchMode = false;

        if (currentBatch && currentBatch->hasCommands())
        {
            redoStack.clear();
            undoStack.push_back(std::move(currentBatch));
            trimUndoStack();
        }
        else
        {
            vfLogInfo("Batch operation had no commands: {}", batchDescription);
        }

        currentBatch.reset();
        batchDescription.clear();
    }
}
