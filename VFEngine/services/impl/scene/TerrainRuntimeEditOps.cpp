#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/BrushSampler.hpp"
#include "terrain/HeightBrushApplicator.hpp"
#include "terrain/WeightBrushApplicator.hpp"
#include "terrain/HoleBrushApplicator.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainRuntimeEditEvents.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "print/Log.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

// VK-1624 runtime terrain edits.
//
// Deliberately NOT built on TerrainService::applyBrush / applyPaintBrush / applyHoleBrush: those
// three read editor tool-mode state through the dispatcher (GetSculptTargetEntityQuery,
// GetPaintTargetEntityQuery, GetHoleTargetEntityQuery, GetBrushParamsQuery ...) whose handlers only
// EditorServiceBootstrap registers, and EventDispatcher::query throws on a missing handler -- so in
// the Runtime the command dispatches fine and the handler body throws. applyBrush is additionally
// GPU-only (brushComputeProvider is null outside the editor) and would no-op even if the queries
// resolved. None of the editor mode-events headers are included here for that reason.
//
// Threading: these run on the "Scripts" frame task, which is not pinned to the main thread
// (RuntimeHandler.cpp / EditorFrameTaskGraph.cpp both use the default). That is safe because they
// only mutate the contents of already-resident tiles. Every mutation of grid STRUCTURE -- tile
// insert / remove / regeneration / collider submission -- happens inside getRawVisibleTiles on the
// Render task, which is transitively ordered after Scripts. That ordering is the whole safety
// argument, so two rules follow and must not be relaxed:
//   1. never call streamInTile from here (it inserts into TerrainGrid::tiles and creates entities)
//   2. never call fileCache->ensureHeightsLoaded from here (file I/O on the gameplay thread)
// A tile that is not resident with heights is skipped and reported as untouched.

namespace services
{
    namespace
    {
        // Mirrors the free-channel test inside TileWeightMapData::assignChannel. Kept in sync by
        // construction: anything with a total weight below EPSILON is considered unused there too.
        constexpr float WEIGHT_CHANNEL_FREE_EPSILON = 0.01f;

        float worldTileSizeOf(terrain::TerrainGrid& grid)
        {
            return grid.getTileConfig().worldTileSize;
        }
    }

    bool TerrainService::canPaintWithoutEviction(const terrain::TileWeightMapData& weightMap,
                                                 uint8_t paletteLayer)
    {
        if (!weightMap.isInitialized())
            return false;

        if (weightMap.findChannel(paletteLayer) != 0xFF)
            return true;

        const size_t texelCount = weightMap.getTexelCount();
        const size_t channelCount = std::min(weightMap.layerWeights.size(),
                                             static_cast<size_t>(terrain::WEIGHT_CHANNELS));
        for (size_t ch = 0; ch < channelCount; ++ch)
        {
            float sum = 0.0f;
            for (size_t i = 0; i < texelCount && i < weightMap.layerWeights[ch].size(); ++i)
                sum += weightMap.layerWeights[ch][i];

            if (sum < WEIGHT_CHANNEL_FREE_EPSILON)
                return true;
        }

        return false;
    }

    void TerrainService::addUniqueTile(std::vector<terrain::TileCoord>& tiles,
                                       const terrain::TileCoord& coord)
    {
        // Linear scan rather than a set: a batch is tens of entries at most, and the same choice is
        // already made for strokeColliderPending.
        if (std::find(tiles.begin(), tiles.end(), coord) == tiles.end())
            tiles.push_back(coord);
    }

    terrain::TerrainGrid* TerrainService::resolveRuntimeEditGrid(const glm::vec2& worldXZ,
                                                                uint64_t& entityIdOut)
    {
        entityIdOut = 0;
        if (terrainGrids.empty())
            return nullptr;

        // Pick the grid that actually owns the tile under the edit centre. getTerrainHeightAt just
        // takes terrainGrids.begin(), which is non-deterministic with two terrains loaded; a write
        // that guessed wrong would deform terrain the script never named.
        terrain::TerrainGrid* fallback = nullptr;
        uint64_t fallbackId = 0;

        for (auto& [entityId, gridPtr] : terrainGrids)
        {
            terrain::TerrainGrid* grid = gridPtr.get();
            if (!grid)
                continue;

            if (!fallback)
            {
                fallback = grid;
                fallbackId = entityId;
            }

            const float tileSize = worldTileSizeOf(*grid);
            if (!(tileSize > 0.0f))
                continue;

            terrain::TileCoord coord{
                static_cast<int32_t>(std::floor(worldXZ.x / tileSize)),
                static_cast<int32_t>(std::floor(worldXZ.y / tileSize))};

            if (grid->hasTile(coord))
            {
                entityIdOut = entityId;
                return grid;
            }
        }

        // Nothing owns that exact tile. With a single terrain in the scene that just means the edit
        // straddles the authored edge, and the per-tile loop will skip whatever is missing; with
        // several it is genuinely ambiguous, so refuse rather than guess.
        if (terrainGrids.size() == 1)
        {
            entityIdOut = fallbackId;
            return fallback;
        }

        return nullptr;
    }

    uint32_t TerrainService::deformTerrainRuntime(const ::events::terrainEdit::DeformTerrainCommand& cmd)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return 0;

        if (!(cmd.radius > 0.0f) || !std::isfinite(cmd.amount) ||
            !std::isfinite(cmd.worldPosition.x) || !std::isfinite(cmd.worldPosition.y))
            return 0;

        uint64_t entityId = 0;
        terrain::TerrainGrid* grid = resolveRuntimeEditGrid(cmd.worldPosition, entityId);
        if (!grid)
            return 0;

        const float worldTileSize = worldTileSizeOf(*grid);
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            cmd.worldPosition, cmd.radius, worldTileSize);

        auto& tiles = runtimeEdit.pending[entityId];
        uint32_t touched = 0;

        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile || !tile->hasHeightData())
                continue;

            terrain::HeightBrushApplicator::ApplyParams params{};
            params.brushCenter = cmd.worldPosition;
            params.tileWorldOrigin = glm::vec2(
                static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                static_cast<float>(tile->coord.z) * tile->config.worldTileSize);
            params.brushRadius = cmd.radius;
            params.vertexSpacing = tile->config.getVertexSpacing();
            params.verticesPerSide = tile->config.getVertexCount();
            params.falloff = cmd.falloff;
            params.shape = cmd.shape;
            params.mode = cmd.mode;
            params.amount = cmd.amount;
            params.minHeight = tile->config.minHeight;
            params.maxHeight = tile->config.maxHeight;

            // VK-1645: DERIVED, deliberately -- a runtime edit never touches the authoritative
            // base, even on a tile under a reserved height layer. It has no undo entry and no
            // markDirty below precisely because it is transient; routing it to the base would
            // let a gameplay crater permanently rewrite authored data with no way back and no
            // way to save it. The cost is that the next recompose of a covered tile wipes the
            // crater, which is the right trade for transient state.
            if (!terrain::HeightBrushApplicator::apply(tile->heightData, params))
                continue;

            // Heights only: NOT topologyDirty, so TerrainTileGenerator::regenerateLOD keeps the
            // meshlet fast path. setAllLODsDirty also sets isDirty and, via setLODGPUDirty during
            // regeneration, refreshes the RVT world-height plane for free.
            tile->isDirty = true;
            tile->setAllLODsDirty();

            // Deliberately no fileCache->markDirty here, unlike every editor brush. A runtime edit
            // is transient: marking it would both invite it into an incremental save of the
            // authored .vfterrain and pin the tile in memory forever, because streamOutTile refuses
            // to unload a dirty coord.
            addUniqueTile(tiles.heightTiles, coord);
            ++touched;
        }

        if (touched == 0 && tiles.heightTiles.empty() && tiles.holeTiles.empty())
            runtimeEdit.pending.erase(entityId);

        return touched;
    }

    uint32_t TerrainService::paintTerrainLayerRuntime(
        const ::events::terrainEdit::PaintTerrainLayerCommand& cmd)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return 0;

        if (!(cmd.radius > 0.0f) ||
            !std::isfinite(cmd.worldPosition.x) || !std::isfinite(cmd.worldPosition.y))
            return 0;

        if (cmd.layerIndex >= static_cast<uint32_t>(terrain::MAX_TERRAIN_LAYERS))
            return 0;

        // SetBaseLayer calls initializeDefault(), which wipes every weight channel of the tile. It
        // is a legitimate authoring tool behind an editor button; it is not something a mistyped
        // script argument should be able to do to shipped terrain.
        if (cmd.paintMode == ::terrain::PaintBrushType::SetBaseLayer)
        {
            vfLogWarning("[TerrainRuntimeEdit] SetBaseLayer is not available to scripts "
                         "(it clears every weight channel of each touched tile)");
            return 0;
        }

        uint64_t entityId = 0;
        terrain::TerrainGrid* grid = resolveRuntimeEditGrid(cmd.worldPosition, entityId);
        if (!grid)
            return 0;

        const auto paletteLayer = static_cast<uint8_t>(cmd.layerIndex);
        const float worldTileSize = worldTileSizeOf(*grid);
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            cmd.worldPosition, cmd.radius, worldTileSize);

        uint32_t touched = 0;

        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile || !tile->weightMap.isInitialized())
                continue;

            if (!canPaintWithoutEviction(tile->weightMap, paletteLayer))
            {
                vfLogWarning("[TerrainRuntimeEdit] Tile ({}, {}) has all {} weight channels in use; "
                             "skipping paint of palette layer {} rather than evicting one",
                             coord.x, coord.z, terrain::WEIGHT_CHANNELS, cmd.layerIndex);
                continue;
            }

            terrain::WeightBrushApplicator::ApplyParams params{};
            params.brushCenter = cmd.worldPosition;
            params.tileWorldOrigin = glm::vec2(
                static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                static_cast<float>(tile->coord.z) * tile->config.worldTileSize);
            params.brushRadius = cmd.radius;
            params.brushStrength = std::clamp(cmd.strength, 0.0f, 1.0f);
            params.brushOpacity = std::clamp(cmd.opacity, 0.0f, 1.0f);
            params.vertexSpacing = tile->config.getVertexSpacing();
            params.verticesPerSide = tile->config.getVertexCount();
            params.falloff = cmd.falloff;
            params.shape = cmd.shape;
            params.brushType = cmd.paintMode;
            params.activeLayer = cmd.layerIndex;
            // The applicator computes influence = falloff * strength * opacity * deltaTime because
            // the editor holds the brush down and wants a rate. A native fires once, so deltaTime is
            // 1 and `strength` is the absolute influence at the brush centre.
            params.deltaTime = 1.0f;
            params.invert = false;

            if (!terrain::WeightBrushApplicator::apply(tile->weightMap, params))
                continue;

            // Weights are neither geometry nor collider input: no LOD dirty, no topology, and the
            // tile never enters the batch. weightMapGPUDirty already coalesces repeated edits.
            tile->weightMapDirty = true;
            tile->weightMapGPUDirty = true;
            ++touched;
        }

        return touched;
    }

    uint32_t TerrainService::setTerrainHolesRuntime(
        const ::events::terrainEdit::SetTerrainHolesCommand& cmd)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return 0;

        if (!(cmd.radius > 0.0f) ||
            !std::isfinite(cmd.worldPosition.x) || !std::isfinite(cmd.worldPosition.y))
            return 0;

        uint64_t entityId = 0;
        terrain::TerrainGrid* grid = resolveRuntimeEditGrid(cmd.worldPosition, entityId);
        if (!grid)
            return 0;

        const float worldTileSize = worldTileSizeOf(*grid);
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            cmd.worldPosition, cmd.radius, worldTileSize);

        auto& tiles = runtimeEdit.pending[entityId];
        uint32_t touched = 0;

        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
                continue;

            // Filling holes on a tile that has no mask is a no-op, and allocating one just to write
            // zeros would cost a full-path meshlet rebuild for nothing.
            if (!tile->hasHoleMask())
            {
                if (!cmd.makeHole)
                    continue;
                tile->initializeHoleMask();
            }

            terrain::HoleBrushApplicator::ApplyParams params{};
            params.brushCenter = cmd.worldPosition;
            params.tileWorldOrigin = glm::vec2(
                static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                static_cast<float>(tile->coord.z) * tile->config.worldTileSize);
            params.brushRadius = cmd.radius;
            params.vertexSpacing = tile->config.getVertexSpacing();
            params.quadsPerSide = tile->config.getVertexCount() - 1;
            // Constant, always: HoleBrushApplicator thresholds the falloff curve at 0.5, so any
            // other curve silently shrinks the effective radius by an undocumented factor. The
            // radius a script asks for is the radius it gets.
            params.falloff = ::terrain::BrushFalloff::Constant;
            params.shape = cmd.shape;
            params.erase = !cmd.makeHole;

            if (!terrain::HoleBrushApplicator::apply(tile->holeMask, params))
                continue;

            // topologyDirty MUST be set after initializeHoleMask above, which clears it. Setting it
            // first would be silently discarded, the meshlet fast path would run, and the mesh would
            // keep its old holes forever.
            tile->topologyDirty = true;
            tile->isDirty = true;
            tile->setAllLODsDirty();

            addUniqueTile(tiles.holeTiles, coord);
            ++touched;
        }

        if (touched == 0 && tiles.heightTiles.empty() && tiles.holeTiles.empty())
            runtimeEdit.pending.erase(entityId);

        return touched;
    }

    void TerrainService::beginRuntimeTerrainEditBatch()
    {
        runtimeEdit.open = true;
        runtimeEdit.openFrames = 0;
    }

    uint32_t TerrainService::flushRuntimeTerrainEdits()
    {
        runtimeEdit.open = false;
        runtimeEdit.openFrames = 0;

        // The seam weld and the collider submission are deferred to the next terrain tick rather
        // than run here: this call arrives on the Scripts task, and both of those touch neighbour
        // tiles and Jolt bodies. The camera position the async collider path needs to pick a physics
        // LOD only exists on the Render path anyway.
        uint32_t pendingTiles = 0;
        for (const auto& [entityId, tiles] : runtimeEdit.pending)
            pendingTiles += static_cast<uint32_t>(tiles.heightTiles.size() + tiles.holeTiles.size());

        return pendingTiles;
    }

    void TerrainService::discardRuntimeTerrainEdits()
    {
        runtimeEdit.clear();
    }

    void TerrainService::drainRuntimeTerrainEdits(const glm::vec3& cameraPosition)
    {
        if (runtimeEdit.empty())
        {
            runtimeEdit.openFrames = 0;
            return;
        }

        if (runtimeEdit.open)
        {
            // Safety net: a script that opened a batch and never flushed (threw, early-returned,
            // or simply forgot) must not leave physics stale forever. Hold the batch for a couple of
            // frames -- long enough for a normal begin/edit/flush within one frame -- then force it.
            constexpr uint32_t MAX_OPEN_FRAMES = 2;
            if (++runtimeEdit.openFrames <= MAX_OPEN_FRAMES)
                return;

            vfLogWarning("[TerrainRuntimeEdit] Edit batch left open for {} frames; draining it. "
                         "Call Terrain::flush() after a burst of edits.", runtimeEdit.openFrames);
            runtimeEdit.open = false;
        }

        if (saveInProgress.load(std::memory_order_acquire))
            return; // Retry next frame; the batch stays pending rather than being lost.

        for (auto& [entityId, tiles] : runtimeEdit.pending)
        {
            auto gridIt = terrainGrids.find(entityId);
            if (gridIt == terrainGrids.end() || !gridIt->second)
                continue;

            terrain::TerrainGrid* grid = gridIt->second.get();
            const EntityHandle terrainEntity{entityId};

            // syncHoleBoundaries / syncBrushBoundaryHeights call captureStrokeTileBefore internally
            // (VK-1615), and that is a no-op only while !strokeActive. Suppress the latch across
            // both: if a user happens to be mid-drag with the sculpt brush in play mode, a script
            // edit must not fold its tiles into the human's stroke -- Ctrl+Z would then revert the
            // script's crater under the label "Sculpt Terrain". strokeBefore / strokeTool /
            // strokeEntityId are untouched, so the human's stroke still closes normally.
            const bool savedStrokeActive = std::exchange(strokeActive, false);
            syncHoleBoundaries(grid, tiles.holeTiles);
            syncBrushBoundaryHeights(grid, tiles.heightTiles);
            strokeActive = savedStrokeActive;

            // Both sync helpers mutate NEIGHBOUR tiles that are not in the caller's list --
            // syncBrushBoundaryHeights writes the +X/+Z sides of each seam, syncHoleBoundaries all
            // four -- so their colliders would otherwise go stale. The editor brushes have the same
            // latent gap; here it would be immediately visible as a collision cliff at a tile border
            // under a one-shot crater, so the neighbours are included.
            std::vector<terrain::TileCoord> colliderTiles;
            colliderTiles.reserve(tiles.heightTiles.size() * 3 + tiles.holeTiles.size() * 5);

            for (const auto& coord : tiles.heightTiles)
            {
                addUniqueTile(colliderTiles, coord);
                addUniqueTile(colliderTiles, terrain::TileCoord{coord.x + 1, coord.z});
                addUniqueTile(colliderTiles, terrain::TileCoord{coord.x, coord.z + 1});
            }
            for (const auto& coord : tiles.holeTiles)
            {
                addUniqueTile(colliderTiles, coord);
                addUniqueTile(colliderTiles, terrain::TileCoord{coord.x + 1, coord.z});
                addUniqueTile(colliderTiles, terrain::TileCoord{coord.x - 1, coord.z});
                addUniqueTile(colliderTiles, terrain::TileCoord{coord.x, coord.z + 1});
                addUniqueTile(colliderTiles, terrain::TileCoord{coord.x, coord.z - 1});
            }

            glm::vec3 worldMin(std::numeric_limits<float>::max());
            glm::vec3 worldMax(std::numeric_limits<float>::lowest());

            const bool hasCollider = physicsProvider && physicsProvider->hasTerrainCollider(terrainEntity);

            for (const auto& coord : colliderTiles)
            {
                terrain::TerrainTile* tile = grid->getTile(coord);
                if (!tile || !tile->hasHeightData())
                    continue;

                const float tileExtent = tile->config.getVertexSpacing()
                    * static_cast<float>(tile->config.getVertexCount() - 1);
                worldMin = glm::min(worldMin, tile->worldOrigin);
                worldMax = glm::max(worldMax, tile->worldOrigin + glm::vec3(tileExtent, 0.0f, tileExtent));

                if (!hasCollider)
                    continue;

                // submitAsyncTerrainTileCollider, NOT rebuildTerrainTileCollider. The synchronous
                // path destroys and recreates the body, and removeTerrainTileBody parks
                // colliderStreamInfos[key].currentLOD at 255 with memoryUsage 0 while
                // addTerrainTileBody never restores either -- so every rebuilt tile is permanently
                // skipped by both the physics LOD transition pass and the eviction pass, after its
                // memory has already been subtracted. Under repeated craters that compounds. The
                // async path refreshes the stream info and keeps the old body live until the
                // replacement shape is ready.
                glm::vec3 tileCenter = tile->worldOrigin
                    + glm::vec3(tileExtent * 0.5f, 0.0f, tileExtent * 0.5f);

                std::vector<float> physicsHeights;
                auto info = buildTileColliderInfo(*tile, terrainEntity, physicsHeights);
                physicsProvider->submitAsyncTerrainTileCollider(
                    terrainEntity, info, glm::length(cameraPosition - tileCenter));
            }

            if (worldMin.x <= worldMax.x)
            {
                ::events::terrainEdit::RuntimeTerrainEditedNotification notification;
                notification.tileCount = static_cast<uint32_t>(colliderTiles.size());
                notification.worldMin = worldMin;
                notification.worldMax = worldMax;
                ::events::EventDispatcher::instance().publish(notification);

                // Reuse the editor brush notification so the two existing consumers pick a runtime
                // edit up unchanged: NavmeshTileManager marks the surrounding navmesh tiles dirty
                // (NavmeshTileManager.cpp:53) and VFXRuntimeAdapter drops its cached terrain
                // heightfield (VFXRuntimeAdapter.cpp:617). One publish per drain, not per edit --
                // a navmesh rebake per shell would cost far more than the craters do.
                ::events::brush::BrushAppliedNotification brushNotification;
                brushNotification.position = (worldMin + worldMax) * 0.5f;
                brushNotification.type = ::terrain::BrushType::Raise;
                ::events::EventDispatcher::instance().publish(brushNotification);
            }

            // Deliberately no per-drain log: a script deforming from onUpdate drains every frame,
            // and the notification above already carries the tile count for anyone who wants it.
        }

        runtimeEdit.clear();
    }
}
