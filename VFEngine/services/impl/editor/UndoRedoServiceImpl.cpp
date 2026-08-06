#include "print/Log.hpp"
#include "UndoRedoServiceImpl.hpp"
#include "../../events/editor/UndoRedoEvents.hpp"
#include "../../events/editor/EditorSettingsEvents.hpp"
#include "../../events/project/SceneEvents.hpp"

namespace services
{
    UndoRedoServiceImpl::UndoRedoServiceImpl()
    {
    }

    UndoRedoServiceImpl::~UndoRedoServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (sceneClearedToken.isValid())
            dispatcher.unsubscribe(sceneClearedToken);
        if (settingsChangedToken.isValid())
            dispatcher.unsubscribe(settingsChangedToken);
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

        dispatcher.registerCommandHandler<events::undoredo::PushUndoableCommand>(
            [this](const events::undoredo::PushUndoableCommand& cmd)
            {
                if (cmd.command)
                    pushCommand(std::make_unique<SharedUndoCommand>(cmd.command));
            });

        dispatcher.registerCommandHandler<events::undoredo::SetUndoHistoryLimitsCommand>(
            [this](const events::undoredo::SetUndoHistoryLimitsCommand& cmd)
            {
                setHistoryLimits(cmd.maxDepth, cmd.maxBytes);
            });

        dispatcher.registerQueryHandler<events::undoredo::GetUndoHistoryStatsQuery>(
            [this](const events::undoredo::GetUndoHistoryStatsQuery&)
            {
                return getHistoryStats();
            });

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                clear();
            });

        // Apply the preference live rather than only at startup, so changing the limit
        // in Preferences takes effect without an editor restart.
        settingsChangedToken = dispatcher.subscribe<events::editor::EditorSettingsChangedNotification>(
            [this](const events::editor::EditorSettingsChangedNotification& notification)
            {
                setHistoryLimits(static_cast<size_t>(notification.settings.undo.maxHistoryDepth),
                                 static_cast<size_t>(notification.settings.undo.maxHistoryBytes));
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

        clearStackBytes(redoStack);

        currentBytes += command->getMemoryFootprint();
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

            // The command just moves between stacks, so currentBytes (which spans both)
            // is unchanged here. Only the redo trim below can drop bytes.
            redoStack.push_back(std::move(command));

            trimRedoStack();

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

            // No net byte change (currentBytes spans both stacks), but the depth cap can
            // still bite: lowering maxHistoryDepth while entries sit in redoStack leaves
            // undoStack.size() + 1 able to exceed it once they are redone.
            undoStack.push_back(std::move(command));

            trimUndoStack();

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
        currentBytes = 0;
        currentBatch.reset();
        batchDescription.clear();
        inBatchMode = false;
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

    void UndoRedoServiceImpl::clearStackBytes(std::vector<std::unique_ptr<IUndoableCommand>>& stack)
    {
        for (const auto& command : stack)
        {
            if (command)
            {
                const size_t bytes = command->getMemoryFootprint();
                currentBytes -= (bytes <= currentBytes) ? bytes : currentBytes;
            }
        }
        stack.clear();
    }

    void UndoRedoServiceImpl::trimUndoStack()
    {
        const auto dropOldest = [this]()
        {
            const size_t bytes = undoStack.front() ? undoStack.front()->getMemoryFootprint() : 0;
            currentBytes -= (bytes <= currentBytes) ? bytes : currentBytes;
            undoStack.erase(undoStack.begin());
        };

        while (maxHistoryDepth != 0 && undoStack.size() > maxHistoryDepth)
        {
            dropOldest();
        }

        // size() > 1 floor: a single stroke larger than the whole budget must still be
        // undoable once, or a small budget on a big terrain leaves the user with no undo
        // at all. The budget is a ceiling on history, not a veto on the last action.
        while (maxHistoryBytes != 0 && undoStack.size() > 1 && currentBytes > maxHistoryBytes)
        {
            dropOldest();
        }
    }

    void UndoRedoServiceImpl::trimRedoStack()
    {
        // Drops from the FRONT, which is the furthest-FUTURE redo: undoing [A,B,C] fully
        // yields redoStack == [C,B,A] with back() == A redone next, so dropping the front
        // is the only thing that keeps the remaining redo chain contiguous.
        const auto dropFurthest = [this]()
        {
            const size_t bytes = redoStack.front() ? redoStack.front()->getMemoryFootprint() : 0;
            currentBytes -= (bytes <= currentBytes) ? bytes : currentBytes;
            redoStack.erase(redoStack.begin());
        };

        while (maxHistoryDepth != 0 && redoStack.size() > maxHistoryDepth)
        {
            dropFurthest();
        }

        // Same size() > 1 floor as trimUndoStack, for the same reason: the action the user
        // just undid must stay redoable even if it alone exceeds the budget, or Ctrl+Z
        // followed by Ctrl+Y would silently do nothing.
        while (maxHistoryBytes != 0 && redoStack.size() > 1 && currentBytes > maxHistoryBytes)
        {
            dropFurthest();
        }
    }

    void UndoRedoServiceImpl::setHistoryLimits(size_t maxDepth, size_t maxBytes)
    {
        maxHistoryDepth = maxDepth;
        maxHistoryBytes = maxBytes;

        // Redo first: it is the cheaper history to lose, so shedding it may bring the
        // total under budget without dropping any undoable entry.
        trimRedoStack();
        trimUndoStack();
    }

    UndoHistoryStats UndoRedoServiceImpl::getHistoryStats() const
    {
#ifdef _DEBUG
        // The incremental accounting has ten mutation sites; a missed subtraction would
        // make the budget monotonically shrink until undo silently stops working. Catch
        // the drift here rather than in a bug report.
        size_t recomputed = 0;
        for (const auto& command : undoStack)
        {
            if (command) recomputed += command->getMemoryFootprint();
        }
        for (const auto& command : redoStack)
        {
            if (command) recomputed += command->getMemoryFootprint();
        }
        if (recomputed != currentBytes)
        {
            vfLogError("Undo byte accounting drifted: tracked={}, actual={}", currentBytes, recomputed);
        }
#endif

        UndoHistoryStats stats;
        stats.undoCount = undoStack.size();
        stats.redoCount = redoStack.size();
        stats.totalBytes = currentBytes;
        stats.maxDepth = maxHistoryDepth;
        stats.maxBytes = maxHistoryBytes;
        return stats;
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
            clearStackBytes(redoStack);
            currentBytes += currentBatch->getMemoryFootprint();
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
