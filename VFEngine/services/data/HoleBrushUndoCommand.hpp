#pragma once
#include "UndoTypes.hpp"
#include "terrain/TerrainTypes.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <functional>

namespace services
{
    struct HoleMaskSnapshot
    {
        terrain::TileCoord coord;
        std::vector<uint8_t> holeMask;
    };

    using HoleMaskRestoreCallback = std::function<void(
        const std::vector<HoleMaskSnapshot>& snapshots)>;

    class HoleBrushUndoCommand : public IUndoableCommand
    {
    private:
        std::vector<HoleMaskSnapshot> beforeSnapshots;
        std::vector<HoleMaskSnapshot> afterSnapshots;
        uint64_t terrainEntityId;
        HoleMaskRestoreCallback restoreCallback;

    public:
        HoleBrushUndoCommand(
            uint64_t entityId,
            std::vector<HoleMaskSnapshot> before,
            std::vector<HoleMaskSnapshot> after,
            HoleMaskRestoreCallback callback)
            : terrainEntityId(entityId)
            , beforeSnapshots(std::move(before))
            , afterSnapshots(std::move(after))
            , restoreCallback(std::move(callback))
        {
        }

        void execute() override
        {
            if (restoreCallback)
                restoreCallback(afterSnapshots);
        }

        void undo() override
        {
            if (restoreCallback)
                restoreCallback(beforeSnapshots);
        }

        std::string getDescription() const override { return "Hole Brush"; }
    };
}
