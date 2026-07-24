#include "MeshBrushUndoCommands.hpp"

#include "../events/EventDispatcher.hpp"
#include "../events/meshbrush/MeshBrushEvents.hpp"

#include <utility>

namespace services
{
    MeshBrushStrokeUndoCommand::MeshBrushStrokeUndoCommand(
        std::vector<meshbrush::MeshBrushInstanceSpec> createdSpecs,
        std::vector<meshbrush::MeshBrushInstanceSpec> removedSpecs)
        : created(std::move(createdSpecs)),
          removed(std::move(removedSpecs))
    {
        if (!created.empty() && removed.empty())
            description = "Paint Mesh Brush";
        else if (created.empty() && !removed.empty())
            description = "Erase Mesh Brush";
        else
            description = "Edit Mesh Brush";
    }

    bool MeshBrushStrokeUndoCommand::hasChanges() const
    {
        return !created.empty() || !removed.empty();
    }

    void MeshBrushStrokeUndoCommand::apply(
        const std::vector<meshbrush::MeshBrushInstanceSpec>& toRemove,
        const std::vector<meshbrush::MeshBrushInstanceSpec>& toRespawn)
    {
        events::meshBrush::ApplyMeshBrushInstanceDeltaCommand command;
        command.removeIds.reserve(toRemove.size());
        for (const auto& spec : toRemove)
            command.removeIds.push_back(spec.instanceId);
        command.respawnSpecs = toRespawn;
        events::EventDispatcher::instance().execute(command);
    }

    void MeshBrushStrokeUndoCommand::execute()
    {
        apply(removed, created);
    }

    void MeshBrushStrokeUndoCommand::undo()
    {
        apply(created, removed);
    }
}
