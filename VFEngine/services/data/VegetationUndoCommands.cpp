#include "VegetationUndoCommands.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/vegetation/GrassEvents.hpp"
#include <algorithm>

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
        // BillboardInstance has no no-padding static_assert, so memcmp is not guaranteed safe;
        // compare field-wise. Any content difference (position/rotation/scale/palette/wind/
        // height/tint/normal/source) counts, so a same-count re-bake/edit is still undoable.
        auto sameInstance = [](const vegetation::BillboardInstance& a,
                               const vegetation::BillboardInstance& b)
        {
            return a.position == b.position && a.rotation == b.rotation && a.scale == b.scale &&
                   a.paletteEntryIndex == b.paletteEntryIndex && a.windPhase == b.windPhase &&
                   a.heightScale == b.heightScale && a.tint == b.tint && a.normal == b.normal &&
                   a.source == b.source;
        };
        for (const auto& t : tiles)
        {
            if (t.before.size() != t.after.size())
                return true;
            if (!std::equal(t.before.begin(), t.before.end(), t.after.begin(), sameInstance))
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
