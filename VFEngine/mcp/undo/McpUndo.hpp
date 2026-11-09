#pragma once

#include "events/EventDispatcher.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "print/Log.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace mcp::undo
{
    // The batch an agent edit pushes. The individual commands throw when their target
    // is gone (entity deleted through a non-undoable path, plugin unloaded, ...).
    // UndoRedoServiceImpl puts a throwing entry back on the stack it came from, so a
    // stale agent entry would block every older Ctrl+Z forever; this batch instead
    // logs the failure and lets the entry be consumed. Editor-native undo commands
    // behave the same way (they skip an entity that no longer exists).
    class McpBatchUndoCommand : public services::BatchUndoCommand
    {
    public:
        using services::BatchUndoCommand::BatchUndoCommand;

        void execute() override
        {
            try
            {
                services::BatchUndoCommand::execute();
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[MCP] Redo of '{}' skipped: {}", getDescription(), e.what());
            }
        }

        void undo() override
        {
            try
            {
                services::BatchUndoCommand::undo();
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[MCP] Undo of '{}' skipped: {}", getDescription(), e.what());
            }
        }
    };

    // VK-1651: one agent edit = one Ctrl+Z. A mutating tool performs its edit,
    // collects the matching undo commands here and calls push() once at the end;
    // a tool that fails (throws or returns an error first) pushes nothing.
    //
    // MCP never dispatches BeginBatch/EndBatch: AsyncFileOperations keeps a batch open
    // across async file work, so a nested Begin would be ignored and our End would
    // close that batch early. A local BatchUndoCommand gives the same grouping.
    // PushUndoableCommand does not execute the command, so the edit is not repeated.
    class UndoRecorder
    {
    public:
        explicit UndoRecorder(std::string description)
            : batch(std::make_shared<McpBatchUndoCommand>(std::move(description)))
        {
        }

        void add(std::unique_ptr<services::IUndoableCommand> command)
        {
            if (command && batch)
            {
                batch->addCommand(std::move(command));
            }
        }

        void push()
        {
            if (!batch || !batch->hasCommands())
            {
                return;
            }
            events::undoredo::PushUndoableCommand command;
            command.command = std::move(batch);
            events::EventDispatcher::instance().execute(command);
        }

        // VK-1654: a multi-step edit failed part-way. Undoes what was recorded so far, newest first
        // (McpBatchUndoCommand logs a step that fails and stops there), and drops the batch, so push()
        // and add() become no-ops. The tool then reports the failure.
        void rollback()
        {
            const std::shared_ptr<McpBatchUndoCommand> recorded = std::move(batch);
            if (recorded && recorded->hasCommands())
            {
                recorded->undo();
            }
        }

    private:
        std::shared_ptr<McpBatchUndoCommand> batch;
    };
}
