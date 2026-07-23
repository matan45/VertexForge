#pragma once
#include "UndoTypes.hpp"
#include "../../utilities/foliage/FoliageTypes.hpp"
#include <vector>
#include <cstdint>

namespace services
{
    // Undo command for a single foliage brush stroke. Captures the full before/after
    // FoliageInstance list of every tile the stroke touched, so undo/redo is robust to
    // the swap-and-pop index churn in tile storage. Generalizes VegetationTileSnapshotUndoCommand.
    class FoliageTileSnapshotUndoCommand : public IUndoableCommand
    {
    private:
        struct TileSnapshot
        {
            int32_t tileX = 0;
            int32_t tileZ = 0;
            std::vector<foliage::FoliageInstance> before;
            std::vector<foliage::FoliageInstance> after;
        };

        std::vector<TileSnapshot> tiles;
        std::string description;

        void applyAll(bool useAfter);

    public:
        explicit FoliageTileSnapshotUndoCommand(std::string desc)
            : description(std::move(desc))
        {
        }

        void addTile(int32_t tileX, int32_t tileZ,
                     std::vector<foliage::FoliageInstance> before,
                     std::vector<foliage::FoliageInstance> after);

        bool hasChanges() const;

        void execute() override; // re-apply (redo)
        void undo() override;    // restore previous state
        std::string getDescription() const override { return description; }
    };
}
