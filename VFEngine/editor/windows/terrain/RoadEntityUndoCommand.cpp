#include "RoadEntityUndoCommand.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"

namespace windows
{
    void RoadEntityUndoCommand::execute()
    {
        // Redo. The entity id changes on respawn, so the handle is refreshed — a stale handle here
        // would make the next undo delete nothing (or, worse, something else that reused the slot).
        root = RoadMeshGenerator::spawnRoad(desc);
    }

    void RoadEntityUndoCommand::undo()
    {
        if (!root.isValid())
            return;

        // Deleting the root takes the chunk children with it.
        events::scene::DeleteEntityCommand deleteCmd;
        deleteCmd.entity = root;
        events::EventDispatcher::instance().execute(deleteCmd);
        root = services::EntityHandle::invalid();
    }

    std::string RoadEntityUndoCommand::getDescription() const
    {
        const std::string& name = desc.params.roadName;
        return "Road \"" + (name.empty() ? std::string("Road") : name) + "\"";
    }

    size_t RoadEntityUndoCommand::getMemoryFootprint() const
    {
        // Only a spawn description — the geometry stayed on disk. Reported anyway because
        // SharedUndoCommand forwards this and a zero would misreport the history budget.
        return desc.meshPath.capacity()
             + desc.params.roadName.capacity()
             + desc.params.roadMaterialPath.capacity()
             + desc.params.road.columns.capacity() * sizeof(terrain::RoadProfileColumn)
             + desc.chunkOrigins.capacity() * sizeof(glm::vec3)
             + desc.controlPoints.capacity() * sizeof(glm::vec3);
    }
}
