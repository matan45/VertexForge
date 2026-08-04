#pragma once
#include "../EventTypes.hpp"
#include "terrain/TerrainWeightMap.hpp"
#include <vector>
#include <cstdint>

// VK-1615 terrain sculpt / weight-paint / hole / ramp stroke undo.
//
// Structurally this mirrors CaveBrushEvents.hpp's CaveTileState + RestoreCaveStateCommand:
// the undo command holds the raw before/after bytes and restores through an event, so it
// never holds a pointer to the terrain service or the grid.
//
// The one deliberate difference is `kinds`. Cave restores SDF and hole mask
// unconditionally, which means it cannot express "leave the hole mask alone" -- and a
// sculpt stroke must not clobber a tile's holes. Vector emptiness cannot carry that
// distinction either, because holeMask is legitimately empty on a tile that has never had
// a hole punched. So the bitmask is authoritative and the vectors are just payload.

namespace events::terrain
{
    enum class StrokeDataKind : uint8_t
    {
        None = 0,
        Heights = 1 << 0,
        Weights = 1 << 1,
        Holes = 1 << 2,
        // VK-1645. The tile was covered by a reserved height layer when the snapshot was taken,
        // so the payload is its AUTHORITATIVE BASE plane rather than the derived composite.
        //
        // A distinct bit, not a bool beside Heights, because one stroke genuinely needs both at
        // once: the brush writes a covered tile's base while syncBrushBoundaryHeights writes an
        // uncovered seam neighbour's derived plane, which for that tile IS the authority. Same
        // command, same dispatch, different bit per tile. It also reuses the per-kind first-touch
        // capture and the per-kind pruning in TerrainStrokeUndoCommand for free.
        BaseHeights = 1 << 3
    };

    [[nodiscard]] inline constexpr uint8_t strokeKindBit(StrokeDataKind kind)
    {
        return static_cast<uint8_t>(kind);
    }

    [[nodiscard]] inline constexpr bool hasStrokeKind(uint8_t kinds, StrokeDataKind kind)
    {
        return (kinds & strokeKindBit(kind)) != 0;
    }

    // One tile's full CPU-side state for the kinds a stroke actually changed.
    //
    // Heights are the in-RAM float array, NOT the quantised uint16 disk encoding: that
    // round-trip is lossy (TerrainCompression.hpp), so reusing it would let the terrain
    // drift a little on every undo.
    struct StrokeTileState
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        uint8_t kinds = 0;

        std::vector<float> heightData;
        // VK-1645 authoritative base plane, carried under the BaseHeights bit.
        std::vector<float> baseHeights;
        // Carries layerWeights + resolution + layerIndices. layerIndices is the per-tile
        // palette indirection, mutated by SetBaseLayer and by channel eviction inside
        // WeightBrushApplicator, so it has to travel with the weights.
        ::terrain::TileWeightMapData weightMap;
        std::vector<uint8_t> holeMask;
    };

    // Restore heights / weights / hole masks for a set of tiles, then flag the tiles for
    // regeneration and rebuild colliders. Dispatched once per undo or redo by
    // TerrainStrokeUndoCommand -- one event for the whole stroke, so one collider pass.
    struct RestoreStrokeStateCommand : ICommand<>
    {
        uint64_t entityId = 0;
        std::vector<StrokeTileState> tiles;

        std::string_view getName() const override { return "RestoreTerrainStrokeState"; }
    };

    // VK-1614 wetness/snow mask restore. The mask is ONE world-anchored RGBA8 image for
    // the whole terrain, not per-tile, so this carries a texel rect on a single channel.
    // Bounded that way because SurfaceMaskBrushApplicator::apply already returns the exact
    // rect it wrote -- the only place in the terrain tools where a sub-region is known
    // rather than guessed.
    struct RestoreSurfaceMaskRegionCommand : ICommand<>
    {
        uint32_t channel = 0;

        // Guard: if the live mask has been recreated at a different resolution since the
        // snapshot, the rect is meaningless and the restore must be skipped.
        uint32_t maskWidth = 0;
        uint32_t maskHeight = 0;

        uint32_t minX = 0;
        uint32_t minZ = 0;
        uint32_t rectWidth = 0;
        uint32_t rectHeight = 0;

        std::vector<uint8_t> texels; // rectWidth * rectHeight, one byte per texel

        std::string_view getName() const override { return "RestoreTerrainSurfaceMaskRegion"; }
    };

    // Closes the open sculpt/paint/hole stroke and pushes its undo entry. One command for
    // all three tools: they are mutually exclusive modes sharing one snapshot map, so one
    // Ctrl+Z should revert whichever was active. (Cave keeps its own FinalizeCaveBrush
    // because that one also punches holes and remeshes.)
    struct FinalizeTerrainStrokeCommand : ICommand<>
    {
        std::string_view getName() const override { return "FinalizeTerrainStroke"; }
    };
}
