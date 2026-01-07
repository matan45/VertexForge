#pragma once
#include "../data/UndoTypes.hpp"
#include <memory>
#include <string>

namespace services
{
    // Undo/Redo service interface - manages command history for undoable operations
    class IUndoRedoService
    {
    public:
        virtual ~IUndoRedoService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;

        // ============================================
        // Command History Management
        // ============================================

        // Push a new undoable command onto the history stack
        // This clears the redo stack
        virtual void pushCommand(std::unique_ptr<IUndoableCommand> command) = 0;

        // Undo the last command
        // Returns true if successful, false if nothing to undo
        virtual bool undo() = 0;

        // Redo the last undone command
        // Returns true if successful, false if nothing to redo
        virtual bool redo() = 0;

        // Clear all history (undo and redo stacks)
        virtual void clear() = 0;

        // ============================================
        // State Queries
        // ============================================

        // Check if there are commands to undo
        virtual bool canUndo() const = 0;

        // Check if there are commands to redo
        virtual bool canRedo() const = 0;

        // Get description of the command that would be undone
        virtual std::string getUndoDescription() const = 0;

        // Get description of the command that would be redone
        virtual std::string getRedoDescription() const = 0;

        // Get the current size of the undo stack
        virtual size_t getUndoStackSize() const = 0;

        // Get the current size of the redo stack
        virtual size_t getRedoStackSize() const = 0;

        // ============================================
        // Batch Operations
        // ============================================

        // Begin a batch operation - commands pushed during batch mode are grouped
        // into a single composite undo command
        virtual void beginBatch(const std::string& description) = 0;

        // End a batch operation - creates composite command from all batched commands
        // If no commands were added during batch, nothing is pushed to the stack
        virtual void endBatch() = 0;

        // Check if currently in batch mode
        virtual bool isInBatchMode() const = 0;

        // ============================================
        // Configuration
        // ============================================

        // Set maximum history depth (0 = unlimited)
        virtual void setMaxHistoryDepth(size_t maxDepth) = 0;

        // Get maximum history depth
        virtual size_t getMaxHistoryDepth() const = 0;
    };
}
