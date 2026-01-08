#pragma once
#include "../data/UndoTypes.hpp"
#include <memory>
#include <string>

namespace services
{
    
    class IUndoRedoService
    {
    public:
        virtual ~IUndoRedoService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;
        
        virtual void pushCommand(std::unique_ptr<IUndoableCommand> command) = 0;
        
        virtual bool undo() = 0;
        
        virtual bool redo() = 0;
        
        virtual void clear() = 0;
        
        virtual bool canUndo() const = 0;
        
        virtual bool canRedo() const = 0;
        
        virtual std::string getUndoDescription() const = 0;
        
        virtual std::string getRedoDescription() const = 0;
        
        virtual void beginBatch(const std::string& description) = 0;
        
        virtual void endBatch() = 0;
    };
}
