#include "FoliageUndoCommands.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/foliage/FoliageEvents.hpp"

namespace services
{
    void FoliageTileSnapshotUndoCommand::addTile(
        int32_t tileX, int32_t tileZ,
        std::vector<foliage::FoliageInstance> before,
        std::vector<foliage::FoliageInstance> after)
    {
        tiles.push_back({tileX, tileZ, std::move(before), std::move(after)});
    }

    bool FoliageTileSnapshotUndoCommand::hasChanges() const
    {
        for (const auto& t : tiles)
        {
            if (t.before.size() != t.after.size())
                return true;
        }
        return false;
    }

    void FoliageTileSnapshotUndoCommand::applyAll(bool useAfter)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        for (const auto& t : tiles)
        {
            events::foliage::SetTileFoliageInstancesCommand cmd;
            cmd.tileX = t.tileX;
            cmd.tileZ = t.tileZ;
            cmd.instances = useAfter ? t.after : t.before;
            dispatcher.execute(cmd);
        }
    }

    void FoliageTileSnapshotUndoCommand::execute()
    {
        applyAll(true);
    }

    void FoliageTileSnapshotUndoCommand::undo()
    {
        applyAll(false);
    }
}
