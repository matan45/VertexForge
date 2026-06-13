#include "VegetationUndoCommands.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/vegetation/GrassEvents.hpp"

namespace services
{
    void VegetationTileSnapshotUndoCommand::addTile(
        int32_t tileX, int32_t tileZ,
        std::vector<vegetation::BillboardInstance> before,
        std::vector<vegetation::BillboardInstance> after)
    {
        tiles.push_back({tileX, tileZ, std::move(before), std::move(after)});
    }

    bool VegetationTileSnapshotUndoCommand::hasChanges() const
    {
        for (const auto& t : tiles)
        {
            if (t.before.size() != t.after.size())
                return true;
        }
        return false;
    }

    void VegetationTileSnapshotUndoCommand::applyAll(bool useAfter)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        for (const auto& t : tiles)
        {
            events::vegetation::SetTileBillboardInstancesCommand cmd;
            cmd.tileX = t.tileX;
            cmd.tileZ = t.tileZ;
            cmd.instances = useAfter ? t.after : t.before;
            dispatcher.execute(cmd);
        }
    }

    void VegetationTileSnapshotUndoCommand::execute()
    {
        applyAll(true);
    }

    void VegetationTileSnapshotUndoCommand::undo()
    {
        applyAll(false);
    }
}
