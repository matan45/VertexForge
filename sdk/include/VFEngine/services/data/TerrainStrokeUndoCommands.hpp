#pragma once
#include "UndoTypes.hpp"
#include "../events/terrain/TerrainStrokeEvents.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainWeightMap.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace services
{
    // Undo command for one terrain sculpt / weight-paint / hole / ramp stroke.
    //
    // Holds the before AND after bytes of every tile the stroke touched -- including tiles
    // only the seam-sync helpers wrote -- for the data kinds that actually changed.
    //
    // Why both endpoints rather than a delta: the forward operations are not invertible.
    // syncBrushBoundaryHeights *averages* two boundary columns, WeightBrushApplicator can
    // evict a channel and renormalise the whole tile, and SetBaseLayer reinitialises the
    // weight map. "Redo = re-run the brush" is also impossible: dabs are deltaTime-scaled,
    // order-dependent and resolved on the GPU.
    //
    // Restore is applied through RestoreStrokeStateCommand so the command never holds a
    // pointer to the terrain service or the grid.
    class TerrainStrokeUndoCommand : public IUndoableCommand
    {
    private:
        struct TileSnapshot
        {
            int32_t tileX = 0;
            int32_t tileZ = 0;
            uint8_t kinds = 0;

            std::vector<float> heightsBefore;
            std::vector<float> heightsAfter;
            // VK-1645: the authoritative base plane, for tiles under a reserved height layer.
            std::vector<float> baseBefore;
            std::vector<float> baseAfter;
            ::terrain::TileWeightMapData weightsBefore;
            ::terrain::TileWeightMapData weightsAfter;
            std::vector<uint8_t> holesBefore;
            std::vector<uint8_t> holesAfter;
        };

        uint64_t entityId;
        std::vector<TileSnapshot> tiles;
        std::string description;

        void applyAll(bool useAfter);

    public:
        TerrainStrokeUndoCommand(uint64_t entity, std::string desc)
            : entityId(entity), description(std::move(desc))
        {
        }

        // Records one tile, keeping only the requested kinds whose bytes actually changed.
        // Returns true if any kind survived, i.e. this tile earned an entry.
        //
        // Pruning here rather than at capture time is what bounds the snapshot: a corner
        // brush reports 4 affected tiles of which 1-2 change, and syncBrushBoundaryHeights
        // unconditionally dirties the +X/+Z neighbours whose boundary is usually already
        // welded.
        //
        // Takes the live tile plus a raw pointer to its post-stroke base plane, rather than a
        // grid, so the command stays unit-testable without a TerrainGrid or a TerrainService.
        // `baseAfter` is null when the tile is not covered by a reserved height layer, which
        // drops the BaseHeights kind.
        bool addTile(int32_t tileX, int32_t tileZ, uint8_t requestedKinds,
                     std::vector<float> heightsBefore,
                     std::vector<float> baseBefore,
                     ::terrain::TileWeightMapData weightsBefore,
                     std::vector<uint8_t> holesBefore,
                     const ::terrain::TerrainTile& after,
                     const std::vector<float>* baseAfter);

        [[nodiscard]] bool hasChanges() const { return !tiles.empty(); }
        [[nodiscard]] size_t getTileCount() const { return tiles.size(); }

        void execute() override; // re-apply (redo)
        void undo() override;    // restore pre-stroke state
        std::string getDescription() const override { return description; }
        size_t getMemoryFootprint() const override;
    };


    // Undo command for one wetness/snow stroke on the VK-1614 surface mask.
    //
    // Separate from TerrainStrokeUndoCommand because the mask is not per-tile: it is one
    // world-anchored RGBA8 image, and a stroke writes a single channel. A dab's dirty rect
    // is reported exactly by SurfaceMaskBrushApplicator::apply, so this snapshot is bounded
    // to the union of those rects instead of the whole image.
    //
    // A paint stroke is either Layers (tiles) or Wetness/Snow (mask) -- applyPaintBrush
    // returns early for the non-Layers targets -- so the two commands are never both live.
    class SurfaceMaskStrokeUndoCommand : public IUndoableCommand
    {
    private:
        uint32_t channel = 0;
        uint32_t maskWidth = 0;
        uint32_t maskHeight = 0;
        uint32_t minX = 0;
        uint32_t minZ = 0;
        uint32_t rectWidth = 0;
        uint32_t rectHeight = 0;
        std::vector<uint8_t> before;
        std::vector<uint8_t> after;
        std::string description;

        void apply(const std::vector<uint8_t>& texels);

    public:
        SurfaceMaskStrokeUndoCommand(uint32_t maskChannel,
                                     uint32_t width, uint32_t height,
                                     uint32_t rectMinX, uint32_t rectMinZ,
                                     uint32_t rectW, uint32_t rectH,
                                     std::vector<uint8_t> beforeTexels,
                                     std::vector<uint8_t> afterTexels,
                                     std::string desc)
            : channel(maskChannel), maskWidth(width), maskHeight(height)
              , minX(rectMinX), minZ(rectMinZ), rectWidth(rectW), rectHeight(rectH)
              , before(std::move(beforeTexels)), after(std::move(afterTexels))
              , description(std::move(desc))
        {
        }

        [[nodiscard]] bool hasChanges() const
        {
            return !before.empty() && before.size() == after.size() && before != after;
        }

        void execute() override;
        void undo() override;
        std::string getDescription() const override { return description; }
        size_t getMemoryFootprint() const override;
    };
}
