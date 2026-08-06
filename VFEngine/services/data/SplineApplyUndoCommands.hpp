#pragma once

#include "UndoTypes.hpp"
#include "../events/terrain/HeightLayerUndoEvents.hpp"
#include "../events/terrain/SplineTerrainEvents.hpp"
#include "../events/terrain/SplineTerrainUndoEvents.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace services
{
    // VK-1621 — the terrain half of undoing one spline apply.
    //
    // Splines sit OUTSIDE the VK-1615 brush-stroke undo system on purpose: the strokeActive latch
    // exists precisely so the spline handlers, which share syncBrushBoundaryHeights with the brush,
    // cannot accumulate snapshots into whatever stroke a brush opens next
    // (TerrainService.hpp:115-120, TerrainStrokeUndoOps.cpp:69-70). So this pushes itself through
    // PushUndoableCommand instead, and the road-entity half is pushed separately by the Editor —
    // a BeginBatch/EndBatch pair around the apply is what collapses the two into one Ctrl+Z.
    //
    // VK-1645 split the two halves apart, because they now have genuinely different natures.
    //
    // HEIGHTS are parametric. The sculpt op registers a reserved layer over the tile's
    // authoritative base, so undo replays a DEFINITION rather than writing bytes back. Nothing
    // per-tile is stored, the result is independent of how many splines overlap or what order they
    // are reverted in, and repeated cycles are bit-identical because compose always restarts from
    // the base. The old form — snapshot the composited plane, write it back on undo — could not do
    // any of that, and under base-vs-derived ownership it would additionally write DERIVED data
    // that the next recompose silently discards.
    //
    // VK-1648 replaced `bool hasHeightLayer` with a snapshot PAIR. The flag meant undo was a
    // visibility flip, which was wrong in two different ways:
    //
    //   * For a first apply it left the layer in the stack, merely hidden. Harmless while nothing
    //     could see the stack; the moment a list panel could, undoing an apply left a ghost row for
    //     a layer the artist never knowingly created.
    //   * For a re-apply, which UPDATES the existing layer in place, hiding reverts past the
    //     artist's previous corridor instead of back to it — so undoing a road regeneration erased
    //     the road's deformation entirely rather than restoring the older shape.
    //
    // With both endpoints captured, undo is "put the layer back the way it was", where "the way it
    // was" is legitimately "not there at all" for a create. Determinism is unaffected: a restore
    // rebuilds `eval` through the same makeSplineCorridorEval every other path uses, and the
    // removal keeps the base, so the tile recomposes to exactly the plane it had before.
    //
    // It costs roughly 32 KB per apply (a 1 km polyline plus its affected coords), which is a real
    // regression against the bool — and three orders of magnitude smaller than the per-tile float
    // planes VK-1645 removed.
    //
    // WEIGHTS still store both endpoints. Paint remains destructive in VK-1645 (edit layers are
    // height-only), and the forward operation is not invertible from its parameters:
    // WeightBrushApplicator can evict a channel and renormalize the whole tile.
    class SplineApplyUndoCommand : public IUndoableCommand
    {
    private:
        std::string description;
        uint64_t splineId = 0;
        events::splineTerrain::HeightLayerSnapshot heightBefore;
        events::splineTerrain::HeightLayerSnapshot heightAfter;
        events::splineTerrain::SplineWeightSnapshot weightsBefore;
        events::splineTerrain::SplineWeightSnapshot weightsAfter;

    public:
        SplineApplyUndoCommand(std::string desc, uint64_t splineId,
                               events::splineTerrain::HeightLayerSnapshot beforeLayer,
                               events::splineTerrain::HeightLayerSnapshot afterLayer,
                               events::splineTerrain::SplineWeightSnapshot beforeWeights,
                               events::splineTerrain::SplineWeightSnapshot afterWeights);

        [[nodiscard]] bool hasSnapshots() const
        {
            return heightBefore.present || heightAfter.present || !weightsBefore.empty();
        }

        void execute() override; // redo
        void undo() override;

        std::string getDescription() const override { return description; }
        size_t getMemoryFootprint() const override;
    };
}
