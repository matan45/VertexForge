#pragma once
#include "UndoTypes.hpp"
#include "../../utilities/vegetation/VegetationTypes.hpp"
#include <vector>
#include <cstdint>

namespace services
{
    // Undo command for a single vegetation brush stroke. Captures the full
    // before/after billboard instance list of every tile the stroke touched,
    // so undo/redo is robust to the swap-and-pop index churn in tile storage.
    class VegetationTileSnapshotUndoCommand : public IUndoableCommand
    {
    private:
        struct TileSnapshot
        {
            int32_t tileX = 0;
            int32_t tileZ = 0;
            std::vector<vegetation::BillboardInstance> before;
            std::vector<vegetation::BillboardInstance> after;
        };

        std::vector<TileSnapshot> tiles;
        std::string description;

        void applyAll(bool useAfter);

    public:
        explicit VegetationTileSnapshotUndoCommand(std::string desc)
            : description(std::move(desc))
        {
        }

        void addTile(int32_t tileX, int32_t tileZ,
                     std::vector<vegetation::BillboardInstance> before,
                     std::vector<vegetation::BillboardInstance> after);

        bool hasChanges() const;

        void execute() override; // re-apply (redo)
        void undo() override;    // restore previous state
        std::string getDescription() const override { return description; }
    };
}
