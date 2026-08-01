#pragma once

#include "UndoTypes.hpp"
#include "../events/terrain/SplineTerrainEvents.hpp"
#include "../events/terrain/SplineTerrainUndoEvents.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace services
{
    using SplineHeightSnapshot =
        std::unordered_map<::terrain::TileCoord, std::vector<float>, ::terrain::TileCoordHash>;

    // VK-1621 — the terrain half of undoing one spline apply.
    //
    // Splines sit OUTSIDE the VK-1615 brush-stroke undo system on purpose: the strokeActive latch
    // exists precisely so the spline handlers, which share syncBrushBoundaryHeights with the brush,
    // cannot accumulate snapshots into whatever stroke a brush opens next
    // (TerrainService.hpp:115-120, TerrainStrokeUndoOps.cpp:69-70). So this pushes itself through
    // PushUndoableCommand instead, and the road-entity half is pushed separately by the Editor —
    // a BeginBatch/EndBatch pair around the apply is what collapses the two into one Ctrl+Z.
    //
    // Both endpoints are stored, like TerrainStrokeUndoCommand: the forward operation is not
    // invertible from its parameters (syncBrushBoundaryHeights averages across tile borders, and
    // WeightBrushApplicator can evict a channel and renormalize), so redo has to replay a
    // recorded result rather than re-derive one.
    class SplineApplyUndoCommand : public IUndoableCommand
    {
    private:
        std::string description;
        SplineHeightSnapshot heightsBefore;
        SplineHeightSnapshot heightsAfter;
        events::splineTerrain::SplineWeightSnapshot weightsBefore;
        events::splineTerrain::SplineWeightSnapshot weightsAfter;

    public:
        SplineApplyUndoCommand(std::string desc,
                               SplineHeightSnapshot beforeHeights, SplineHeightSnapshot afterHeights,
                               events::splineTerrain::SplineWeightSnapshot beforeWeights,
                               events::splineTerrain::SplineWeightSnapshot afterWeights);

        [[nodiscard]] bool hasSnapshots() const
        {
            return !heightsBefore.empty() || !weightsBefore.empty();
        }

        void execute() override; // redo
        void undo() override;

        std::string getDescription() const override { return description; }
        size_t getMemoryFootprint() const override;
    };
}
