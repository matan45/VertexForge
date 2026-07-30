#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainSurfaceMaskAsset.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../data/TerrainStrokeUndoCommands.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainStrokeEvents.hpp"
#include "../../events/editor/UndoRedoEvents.hpp"
#include <memory>
#include <utility>

// VK-1615 sculpt / weight-paint / hole / ramp / surface-mask stroke undo.
//
// Mirrors the cave brush (TerrainCaveBrushOps.cpp): the service latches a stroke, captures
// each tile's pre-stroke bytes the first time the stroke touches it, and drains the whole
// lot into one undo command when the stroke finalizes. Restore goes back through an event
// so the undo command never holds a pointer to this service or to the grid.

namespace services
{
    namespace
    {
        using Kind = ::events::terrain::StrokeDataKind;
    }

    const char* TerrainService::strokeLabelFor(StrokeTool tool)
    {
        switch (tool)
        {
        case StrokeTool::Sculpt:      return "Sculpt Terrain";
        case StrokeTool::Ramp:        return "Ramp Terrain";
        case StrokeTool::Paint:       return "Paint Terrain";
        case StrokeTool::Hole:        return "Terrain Holes";
        case StrokeTool::SurfaceMask: return "Paint Surface Mask";
        case StrokeTool::None:
        default:                      return "Edit Terrain";
        }
    }

    void TerrainService::beginStroke(uint64_t entityId, StrokeTool tool, bool isFirstApplication)
    {
        // isFirst auto-flush, borrowed from the mesh brush: a stroke whose Finalize never
        // arrived (viewport collapsed mid-drag, tool or entity switched) is pushed here
        // rather than silently discarded. finalizeTerrainStroke() is a no-op when no
        // stroke is open.
        if (isFirstApplication || strokeEntityId != entityId || strokeTool != tool)
            finalizeTerrainStroke();

        if (!strokeActive)
        {
            strokeBefore.clear();
            strokeMaskBefore.clear();
            strokeMaskDirty = {};
            strokeMaskWidth = 0;
            strokeMaskHeight = 0;
            strokeMaskChannel = 0;
            strokeEntityId = entityId;
            strokeTool = tool;
            strokeActive = true;
        }
    }

    void TerrainService::captureStrokeTileBefore(const terrain::TerrainTile& tile, uint8_t kinds)
    {
        // No stroke open => no capture. This guard is what keeps the spline tool, which
        // shares syncBrushBoundaryHeights, from accumulating into a brush stroke's entry.
        if (!strokeActive)
            return;

        auto& entry = strokeBefore[tile.coord]; // default-constructs on first touch

        // Per-KIND first touch, not per-tile: applyBrush captures Heights for tile T on
        // dab 1, then syncBrushBoundaryHeights captures Heights for T's +X neighbour on
        // the same dab. Neither is re-captured on later dabs, so "before" stays pre-stroke.
        if (::events::terrain::hasStrokeKind(kinds, Kind::Heights)
            && !::events::terrain::hasStrokeKind(entry.kinds, Kind::Heights))
        {
            entry.heightData = tile.heightData;
            entry.kinds |= ::events::terrain::strokeKindBit(Kind::Heights);
        }

        if (::events::terrain::hasStrokeKind(kinds, Kind::Weights)
            && !::events::terrain::hasStrokeKind(entry.kinds, Kind::Weights))
        {
            entry.weightMap = tile.weightMap;
            entry.kinds |= ::events::terrain::strokeKindBit(Kind::Weights);
        }

        if (::events::terrain::hasStrokeKind(kinds, Kind::Holes)
            && !::events::terrain::hasStrokeKind(entry.kinds, Kind::Holes))
        {
            entry.holeMask = tile.holeMask;
            entry.kinds |= ::events::terrain::strokeKindBit(Kind::Holes);
        }
    }

    void TerrainService::beginSurfaceMaskStroke(uint64_t entityId, uint32_t channel,
                                                bool isFirstApplication)
    {
        // Paint target switched mid-drag (Wetness -> Snow): close the open stroke so each
        // channel gets its own undo entry instead of one entry claiming both.
        if (strokeActive && strokeTool == StrokeTool::SurfaceMask
            && !strokeMaskBefore.empty() && strokeMaskChannel != channel)
        {
            finalizeTerrainStroke();
        }

        beginStroke(entityId, StrokeTool::SurfaceMask, isFirstApplication);

        if (!strokeActive || !surfaceMask || !surfaceMask->isValid())
            return;
        if (!strokeMaskBefore.empty())
            return; // already captured for this stroke

        strokeMaskWidth = surfaceMask->width;
        strokeMaskHeight = surfaceMask->height;
        strokeMaskChannel = channel;

        // One byte per texel for the painted channel only. The mask is RGBA8 and a dab
        // writes a single channel, so this is a quarter of the image and 1/8th of a
        // before+after RGBA snapshot.
        const size_t texelCount = surfaceMask->texelCount();
        strokeMaskBefore.resize(texelCount);
        const auto& rgba = surfaceMask->rgba;
        for (size_t texel = 0; texel < texelCount; ++texel)
            strokeMaskBefore[texel] = rgba[texel * terrain::SURFACE_MASK_CHANNELS + channel];
    }

    void TerrainService::accumulateStrokeMaskDirty(
        const terrain::SurfaceMaskBrushApplicator::DirtyRect& rect)
    {
        if (!strokeActive || rect.isEmpty())
            return;

        // add() takes texels but unions AABBs correctly given the two corners, and handles
        // the empty-target case itself.
        strokeMaskDirty.add(rect.minX, rect.minZ);
        strokeMaskDirty.add(rect.maxX, rect.maxZ);
    }

    void TerrainService::discardTerrainStroke()
    {
        strokeBefore.clear();
        strokeMaskBefore.clear();
        strokeMaskDirty = {};
        strokeMaskWidth = 0;
        strokeMaskHeight = 0;
        strokeMaskChannel = 0;
        strokeActive = false;
        strokeTool = StrokeTool::None;
    }

    void TerrainService::finalizeTerrainStroke()
    {
        if (!strokeActive)
            return;

        // Cleared up front as a reentrancy guard: PushUndoableCommand dispatches
        // synchronously, and a handler that ends up calling back in here must not see an
        // open stroke.
        //
        // Deliberately NOT gated on saveInProgress, unlike finalizeCaveBrush: the three
        // apply functions already refuse to mutate during a save, so any snapshot we hold
        // describes real dabs that landed before the save started and must still become an
        // undo entry.
        strokeActive = false;
        const StrokeTool tool = strokeTool;
        strokeTool = StrokeTool::None;

        auto& dispatcher = events::EventDispatcher::instance();

        if (tool == StrokeTool::SurfaceMask)
        {
            if (strokeMaskBefore.empty() || strokeMaskDirty.isEmpty()
                || !surfaceMask || !surfaceMask->isValid()
                || surfaceMask->width != strokeMaskWidth
                || surfaceMask->height != strokeMaskHeight)
            {
                discardTerrainStroke();
                return;
            }

            const uint32_t rectWidth = strokeMaskDirty.maxX - strokeMaskDirty.minX + 1;
            const uint32_t rectHeight = strokeMaskDirty.maxZ - strokeMaskDirty.minZ + 1;

            std::vector<uint8_t> before;
            std::vector<uint8_t> after;
            before.reserve(static_cast<size_t>(rectWidth) * rectHeight);
            after.reserve(static_cast<size_t>(rectWidth) * rectHeight);

            const auto& rgba = surfaceMask->rgba;
            for (uint32_t z = strokeMaskDirty.minZ; z <= strokeMaskDirty.maxZ; ++z)
            {
                const size_t row = static_cast<size_t>(z) * strokeMaskWidth;
                for (uint32_t x = strokeMaskDirty.minX; x <= strokeMaskDirty.maxX; ++x)
                {
                    const size_t texel = row + x;
                    before.push_back(strokeMaskBefore[texel]);
                    after.push_back(rgba[texel * terrain::SURFACE_MASK_CHANNELS + strokeMaskChannel]);
                }
            }

            auto undoCmd = std::make_shared<SurfaceMaskStrokeUndoCommand>(
                strokeMaskChannel, strokeMaskWidth, strokeMaskHeight,
                strokeMaskDirty.minX, strokeMaskDirty.minZ, rectWidth, rectHeight,
                std::move(before), std::move(after), strokeLabelFor(tool));

            if (undoCmd->hasChanges())
            {
                events::undoredo::PushUndoableCommand pushCmd;
                pushCmd.command = undoCmd;
                dispatcher.execute(pushCmd);
            }

            discardTerrainStroke();
            return;
        }

        if (strokeBefore.empty())
        {
            discardTerrainStroke();
            return;
        }

        auto gridIt = terrainGrids.find(strokeEntityId);
        if (gridIt == terrainGrids.end())
        {
            discardTerrainStroke();
            return;
        }
        terrain::TerrainGrid* grid = gridIt->second.get();

        auto undoCmd = std::make_shared<TerrainStrokeUndoCommand>(
            strokeEntityId, strokeLabelFor(tool));

        for (auto& [coord, before] : strokeBefore)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
                continue; // streamed out mid-stroke: nothing left to restore into

            // addTile drops any kind whose bytes did not actually change, which is what
            // bounds the snapshot: the affected-tile query and the seam helpers both
            // over-report.
            undoCmd->addTile(coord.x, coord.z, before.kinds,
                             std::move(before.heightData),
                             std::move(before.weightMap),
                             std::move(before.holeMask),
                             *tile);
        }

        if (undoCmd->hasChanges())
        {
            events::undoredo::PushUndoableCommand pushCmd;
            pushCmd.command = undoCmd;
            dispatcher.execute(pushCmd);
        }

        discardTerrainStroke();
    }

    void TerrainService::restoreStrokeState(
        uint64_t entityId, const std::vector<::events::terrain::StrokeTileState>& tiles)
    {
        auto gridIt = terrainGrids.find(entityId);
        if (gridIt == terrainGrids.end())
            return;
        terrain::TerrainGrid* grid = gridIt->second.get();

        auto cacheIt = fileCaches.find(entityId);
        auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        // Tiles whose collider input (heights and/or hole mask) changed. Weight-only tiles
        // are excluded: the collider is a heightfield built from heights + holeMask, and
        // applyPaintBrush does not rebuild colliders either, so rebuilding on undo would be
        // asymmetric with the forward operation.
        std::vector<terrain::TileCoord> geometryTiles;
        bool anyTile = false;

        for (const auto& state : tiles)
        {
            terrain::TileCoord coord{state.tileX, state.tileZ};
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
                continue;

            anyTile = true;
            bool geometryChanged = false;

            if (::events::terrain::hasStrokeKind(state.kinds, Kind::Heights))
            {
                // Size guard: if the tile resolution changed between snapshot and restore,
                // writing the old array would resize it under the mesher and the
                // heightfield collider.
                if (tile->heightData.empty() || tile->heightData.size() == state.heightData.size())
                {
                    tile->heightData = state.heightData;
                    geometryChanged = true;
                }
            }

            if (::events::terrain::hasStrokeKind(state.kinds, Kind::Holes))
            {
                // May legitimately be empty ("this tile had no hole mask before the
                // stroke"); the kinds bit is what authorises the write.
                tile->holeMask = state.holeMask;
                tile->topologyDirty = true; // forces the full meshlet rebuild path
                geometryChanged = true;
            }

            if (::events::terrain::hasStrokeKind(state.kinds, Kind::Weights))
            {
                // Same resolution guard as heights: the weight-map resolution tracks the
                // tile vertex count, so a mismatch means the tile was rebuilt at a
                // different resolution and the snapshot no longer describes it.
                if (!tile->hasWeightMap()
                    || tile->weightMap.resolution == state.weightMap.resolution)
                {
                    tile->weightMap = state.weightMap; // channels + resolution + layerIndices
                    tile->weightMapDirty = true;
                    tile->weightMapGPUDirty = true;
                }
            }

            if (geometryChanged)
            {
                tile->isDirty = true;
                tile->setAllLODsDirty();
                geometryTiles.push_back(coord);
            }

            // Incremental save keys off TerrainFileCache::dirtyCoords, which also blocks
            // geometry eviction. Without this an undone tile can be written out stale, or
            // evicted from under a later redo. (restoreCaveState omits this; see VK-1615.)
            if (fileCache)
                fileCache->markDirty(coord);
        }

        if (!anyTile)
            return;

        // Deliberately does NOT re-run syncBrushBoundaryHeights / syncHoleBoundaries. Every
        // tile those helpers wrote is itself in the snapshot, and the pre-stroke state was
        // seam-consistent by construction. Re-averaging a restored boundary against a
        // neighbour that is also being restored in the same pass would drift the seam a
        // little on every undo/redo cycle.
        EntityHandle entity{entityId};
        if (!geometryTiles.empty())
        {
            rebuildModifiedColliders(entity, grid, geometryTiles); // also sets saveDirty
        }
        else
        {
            // Weights-only: no collider work, but the scene is still unsaved. Mirrors what
            // applyPaintBrush does at the end of a paint stroke.
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(entity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).saveDirty = true;
            }
        }
    }

    void TerrainService::restoreSurfaceMaskRegion(
        const ::events::terrain::RestoreSurfaceMaskRegionCommand& cmd)
    {
        if (!surfaceMask || !surfaceMask->isValid())
            return;
        if (cmd.channel >= terrain::SURFACE_MASK_CHANNELS)
            return;
        // The mask was recreated or reloaded at a different resolution since the snapshot,
        // so the stored rect no longer addresses the same texels.
        if (surfaceMask->width != cmd.maskWidth || surfaceMask->height != cmd.maskHeight)
            return;
        if (cmd.rectWidth == 0 || cmd.rectHeight == 0)
            return;
        if (cmd.texels.size() != static_cast<size_t>(cmd.rectWidth) * cmd.rectHeight)
            return;
        if (cmd.minX + cmd.rectWidth > surfaceMask->width
            || cmd.minZ + cmd.rectHeight > surfaceMask->height)
            return;

        auto& rgba = surfaceMask->rgba;
        for (uint32_t row = 0; row < cmd.rectHeight; ++row)
        {
            const size_t maskRow = static_cast<size_t>(cmd.minZ + row) * surfaceMask->width;
            const size_t srcRow = static_cast<size_t>(row) * cmd.rectWidth;
            for (uint32_t col = 0; col < cmd.rectWidth; ++col)
            {
                const size_t texel = maskRow + cmd.minX + col;
                rgba[texel * terrain::SURFACE_MASK_CHANNELS + cmd.channel] = cmd.texels[srcRow + col];
            }
        }

        // The existing whole-image re-upload gate; the mask is one texture and a partial
        // copy would need its own staging slice per stroke.
        surfaceMaskPixelsDirty.store(true, std::memory_order_release);
    }
}
