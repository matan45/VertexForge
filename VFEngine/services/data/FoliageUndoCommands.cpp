#include "FoliageUndoCommands.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/foliage/FoliageEvents.hpp"
#include <cstring>

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
            // FoliageInstance is a frozen 56-byte, no-padding, trivially-copyable struct
            // (static_asserts in FoliageTypes.hpp), so memcmp is an exact content compare.
            // Without it a same-count re-bake/edit (placement/appearance changed, count kept)
            // would be treated as "no change" and the undo entry silently dropped.
            if (!t.before.empty() &&
                std::memcmp(t.before.data(), t.after.data(),
                            t.before.size() * sizeof(foliage::FoliageInstance)) != 0)
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
