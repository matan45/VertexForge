#include "UndoRedoServiceImpl.hpp"
#include "print/EditorLogger.hpp"

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

        dispatcher.registerCommandHandler<events::undoredo::ClearHistoryCommand>(
            [this](const events::undoredo::ClearHistoryCommand&)
            {
                clear();
            });

        dispatcher.registerQueryHandler<events::undoredo::CanUndoQuery>(
            [this](const events::undoredo::CanUndoQuery&)
            {
                return canUndo();
            });

        dispatcher.registerQueryHandler<events::undoredo::CanRedoQuery>(
            [this](const events::undoredo::CanRedoQuery&)
            {
                return canRedo();
            });

        dispatcher.registerQueryHandler<events::undoredo::GetUndoDescriptionQuery>(
            [this](const events::undoredo::GetUndoDescriptionQuery&)
            {
                return getUndoDescription();
            });

        dispatcher.registerQueryHandler<events::undoredo::GetRedoDescriptionQuery>(
            [this](const events::undoredo::GetRedoDescriptionQuery&)
            {
                return getRedoDescription();
            });
    }

    void UndoRedoServiceImpl::pushCommand(std::unique_ptr<IUndoableCommand> command)
    {
        if (!command)
        {
            return;
        }

        // If in batch mode, add to current batch instead of undo stack
        if (inBatchMode && currentBatch)
        {
            vfLogInfo("Adding to batch: {}", command->getDescription());
            currentBatch->addCommand(std::move(command));
            return;
        }

        // Clear redo stack when new command is pushed
        redoStack.clear();

        // Add command to undo stack
        undoStack.push_back(std::move(command));

        // Trim if necessary
        trimUndoStack();

        // Notify listeners
        publishStateChanged();

        vfLogInfo("Pushed undo command: {}", undoStack.back()->getDescription());
    }

    bool UndoRedoServiceImpl::undo()
    {
        if (!canUndo())
        {
            return false;
        }

        // Pop from undo stack
        auto command = std::move(undoStack.back());
        undoStack.pop_back();

        std::string description = command->getDescription();

        try
        {
            // Execute undo
            command->undo();

            // Move to redo stack
            redoStack.push_back(std::move(command));

            // Notify listeners
            publishStateChanged();

            // Publish undo performed notification
            events::undoredo::UndoPerformedNotification notification;
            notification.description = description;
            events::EventDispatcher::instance().publish(notification);

            vfLogInfo("Undo: {}", description);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Undo failed: {}", e.what());
            // Put command back on undo stack
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

        // Pop from redo stack
        auto command = std::move(redoStack.back());
        redoStack.pop_back();

        std::string description = command->getDescription();

        try
        {
            // Execute redo
            command->execute();

            // Move to undo stack
            undoStack.push_back(std::move(command));

            // Notify listeners
            publishStateChanged();

            // Publish redo performed notification
            events::undoredo::RedoPerformedNotification notification;
            notification.description = description;
            events::EventDispatcher::instance().publish(notification);

            vfLogInfo("Redo: {}", description);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Redo failed: {}", e.what());
            // Put command back on redo stack
            redoStack.push_back(std::move(command));
            return false;
        }
    }

    void UndoRedoServiceImpl::clear()
    {
        undoStack.clear();
        redoStack.clear();
        publishStateChanged();
        vfLogInfo("Undo history cleared");
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

    size_t UndoRedoServiceImpl::getUndoStackSize() const
    {
        return undoStack.size();
    }

    size_t UndoRedoServiceImpl::getRedoStackSize() const
    {
        return redoStack.size();
    }

    void UndoRedoServiceImpl::setMaxHistoryDepth(size_t maxDepth)
    {
        maxHistoryDepth = maxDepth;
        trimUndoStack();
    }

    size_t UndoRedoServiceImpl::getMaxHistoryDepth() const
    {
        return maxHistoryDepth;
    }

    void UndoRedoServiceImpl::publishStateChanged()
    {
        events::undoredo::UndoStateChangedNotification notification;
        notification.canUndo = canUndo();
        notification.canRedo = canRedo();
        notification.undoDescription = getUndoDescription();
        notification.redoDescription = getRedoDescription();

        events::EventDispatcher::instance().publish(notification);
    }

    void UndoRedoServiceImpl::trimUndoStack()
    {
        if (maxHistoryDepth == 0)
        {
            return; // Unlimited
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
        vfLogInfo("Started batch operation: {}", description);
    }

    void UndoRedoServiceImpl::endBatch()
    {
        if (!inBatchMode)
        {
            vfLogWarning("Not in batch mode, ignoring endBatch call");
            return;
        }

        inBatchMode = false;

        // Only push if there are commands in the batch
        if (currentBatch && currentBatch->hasCommands())
        {
            vfLogInfo("Completed batch operation: {}", batchDescription);
            // Use the base pushCommand logic (not batch mode anymore)
            redoStack.clear();
            undoStack.push_back(std::move(currentBatch));
            trimUndoStack();
            publishStateChanged();
        }
        else
        {
            vfLogInfo("Batch operation had no commands: {}", batchDescription);
        }

        currentBatch.reset();
        batchDescription.clear();
    }

    bool UndoRedoServiceImpl::isInBatchMode() const
    {
        return inBatchMode;
    }
}
