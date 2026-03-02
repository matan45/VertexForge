#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/BrushSampler.hpp"
#include "terrain/WeightBrushApplicator.hpp"
#include "terrain/HoleBrushApplicator.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "resource/ResourceManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/BrushEvents.hpp"
#include "../../events/PaintBrushEvents.hpp"
#include "../../events/HoleBrushEvents.hpp"
#include "../../events/SculptModeEvents.hpp"
#include "../../events/PaintModeEvents.hpp"
#include "../../events/HoleModeEvents.hpp"
#include "print/EditorLogger.hpp"

namespace services
{
    void TerrainService::applyBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::sculpt::GetSculptTargetEntityQuery{});
        if (!targetEntity.has_value())
            return;

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
            return;

        terrain::TerrainGrid* grid = gridIt->second.get();

        auto brushType = dispatcher.query(events::brush::GetBrushTypeQuery{});
        auto brushParams = dispatcher.query(events::brush::GetBrushParamsQuery{});

        if (brushType == terrain::BrushType::Flatten)
        {
            if (isFirstApplication)
            {
                flattenTargetCaptured = true;
                flattenTargetHeight = worldPosition.y;
            }
        }
        else
        {
            flattenTargetCaptured = false;
        }

        glm::vec2 brushCenter(worldPosition.x, worldPosition.z);
        float worldTileSize = 32.0f;
        const auto& allTiles = grid->getAllTiles();
        if (!allTiles.empty())
        {
            worldTileSize = allTiles[0]->config.worldTileSize;
        }
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            brushCenter, brushParams.radius, worldTileSize);

        auto cacheIt = fileCaches.find(targetEntity->id);
        auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        std::vector<terrain::TileCoord> modifiedTiles;
        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
                continue;

            if (!brushComputeProvider)
                continue;

            if (fileCache && !tile->hasHeightData())
            {
                if (!fileCache->ensureHeightsLoaded(*tile))
                    continue;
            }
            if (fileCache)
                fileCache->markDirty(coord);

            terrain::BrushGPUParams gpuParams;
            gpuParams.brushCenter = brushCenter;
            gpuParams.tileWorldOrigin = glm::vec2(
                static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                static_cast<float>(tile->coord.z) * tile->config.worldTileSize);
            gpuParams.brushRadius = brushParams.radius;
            gpuParams.brushStrength = brushParams.strength;
            gpuParams.vertexSpacing = tile->config.getVertexSpacing();
            gpuParams.verticesPerSide = tile->config.getVertexCount();
            gpuParams.falloff = brushParams.falloff;
            gpuParams.shape = brushParams.shape;
            gpuParams.brushType = brushType;
            gpuParams.deltaTime = deltaTime;
            gpuParams.targetHeight = flattenTargetHeight;
            gpuParams.minHeight = tile->config.minHeight;
            gpuParams.maxHeight = tile->config.maxHeight;
            gpuParams.invert = invert;

            if (brushComputeProvider->applyBrushGPU(tile->heightData, gpuParams))
            {
                tile->isDirty = true;
                tile->setAllLODsDirty();
                modifiedTiles.push_back(coord);
            }
            else
            {
                vfLogError("GPU brush application failed for tile ({}, {})", coord.x, coord.z);
            }
        }

        events::brush::BrushAppliedNotification notification;
        notification.position = worldPosition;
        notification.type = brushType;
        dispatcher.publish(notification);

        rebuildModifiedColliders(*targetEntity, grid, modifiedTiles);
    }

    void TerrainService::applyPaintBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::paint::GetPaintTargetEntityQuery{});
        if (!targetEntity.has_value())
            return;

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
            return;

        terrain::TerrainGrid* grid = gridIt->second.get();

        auto brushParams = dispatcher.query(events::paintBrush::GetPaintBrushParamsQuery{});
        auto brushType = dispatcher.query(events::paintBrush::GetPaintBrushTypeQuery{});

        float worldTileSize = 32.0f;
        const auto& allTiles = grid->getAllTiles();
        if (!allTiles.empty())
        {
            worldTileSize = allTiles[0]->config.worldTileSize;
        }

        uint16_t overlayMask = getOverlayMask();

        glm::vec2 brushCenter(worldPosition.x, worldPosition.z);
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            brushCenter, brushParams.radius, worldTileSize);

        auto cacheIt = fileCaches.find(targetEntity->id);
        auto paintFileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
                continue;

            if (paintFileCache && !tile->hasHeightData())
                paintFileCache->ensureHeightsLoaded(*tile);
            if (paintFileCache)
                paintFileCache->markDirty(coord);

            if (!tile->hasWeightMap())
                continue;

            if (brushParams.activeLayer >= terrain::MAX_TERRAIN_LAYERS)
                continue;

            if (brushParams.activeLayer >= tile->weightMap.layerWeights.size())
            {
                tile->weightMap.setLayerCount(static_cast<uint8_t>(brushParams.activeLayer + 1));
                tile->weightMapGPUDirty = true;
            }

            terrain::WeightBrushApplicator::ApplyParams applyParams;
            applyParams.brushCenter = brushCenter;
            applyParams.tileWorldOrigin = glm::vec2(
                static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                static_cast<float>(tile->coord.z) * tile->config.worldTileSize);
            applyParams.brushRadius = brushParams.radius;
            applyParams.brushStrength = brushParams.strength;
            applyParams.brushOpacity = brushParams.opacity;
            applyParams.vertexSpacing = tile->config.getVertexSpacing();
            applyParams.verticesPerSide = tile->config.getVertexCount();
            applyParams.falloff = brushParams.falloff;
            applyParams.shape = brushParams.shape;
            applyParams.brushType = brushType;
            applyParams.activeLayer = brushParams.activeLayer;
            applyParams.deltaTime = deltaTime;
            applyParams.invert = invert;
            applyParams.overlayMask = overlayMask;

            if (terrain::WeightBrushApplicator::apply(tile->weightMap, applyParams))
            {
                tile->weightMapDirty = true;
                tile->weightMapGPUDirty = true;
            }
        }

        events::paintBrush::PaintBrushAppliedNotification paintNotification;
        paintNotification.position = worldPosition;
        paintNotification.type = brushType;
        dispatcher.publish(paintNotification);

        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(*targetEntity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).saveDirty = true;
            }
        }
    }

    void TerrainService::applyHoleBrush(const glm::vec3& worldPosition, bool erase, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::hole::GetHoleTargetEntityQuery{});
        if (!targetEntity.has_value())
            return;

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
            return;

        terrain::TerrainGrid* grid = gridIt->second.get();

        // If starting a new drag, finalize previous undo command and capture new "before" state
        if (isFirstApplication)
        {
            if (holeBrushDragActive)
            {
                finalizeHoleBrushUndo(grid);
            }
            holeBrushDragActive = true;
            holeBrushUndoEntityId = targetEntity->id;
            holeBrushBeforeSnapshots.clear();
            holeBrushAfterMasks.clear();
        }

        auto brushParams = dispatcher.query(events::holeBrush::GetHoleBrushParamsQuery{});

        float worldTileSize = 32.0f;
        const auto& allTiles = grid->getAllTiles();
        if (!allTiles.empty())
        {
            worldTileSize = allTiles[0]->config.worldTileSize;
        }

        glm::vec2 brushCenter(worldPosition.x, worldPosition.z);
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            brushCenter, brushParams.radius, worldTileSize);

        auto cacheIt = fileCaches.find(targetEntity->id);
        auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        // Capture "before" snapshots for tiles not yet tracked in this drag
        if (isFirstApplication)
        {
            captureHoleMaskBefore(grid, affectedTiles);
        }
        else
        {
            // Capture any newly affected tiles
            for (const auto& coord : affectedTiles)
            {
                uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) << 32)
                              | static_cast<uint64_t>(static_cast<uint32_t>(coord.z));
                if (holeBrushAfterMasks.find(key) == holeBrushAfterMasks.end())
                {
                    terrain::TerrainTile* tile = grid->getTile(coord);
                    if (tile)
                    {
                        HoleMaskSnapshot snap;
                        snap.coord = coord;
                        snap.holeMask = tile->hasHoleMask() ? tile->holeMask
                                                            : std::vector<uint8_t>();
                        holeBrushBeforeSnapshots.push_back(std::move(snap));
                    }
                }
            }
        }

        std::vector<terrain::TileCoord> modifiedTiles;
        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
                continue;

            if (fileCache && !tile->hasHeightData())
            {
                if (!fileCache->ensureHeightsLoaded(*tile))
                    continue;
            }

            if (!tile->hasHoleMask())
                tile->initializeHoleMask();

            if (fileCache)
                fileCache->markDirty(coord);

            terrain::HoleBrushApplicator::ApplyParams applyParams;
            applyParams.brushCenter = brushCenter;
            applyParams.tileWorldOrigin = glm::vec2(
                static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                static_cast<float>(tile->coord.z) * tile->config.worldTileSize);
            applyParams.brushRadius = brushParams.radius;
            applyParams.vertexSpacing = tile->config.getVertexSpacing();
            applyParams.verticesPerSide = tile->config.getVertexCount();
            applyParams.falloff = brushParams.falloff;
            applyParams.shape = brushParams.shape;
            applyParams.erase = erase;

            if (terrain::HoleBrushApplicator::apply(tile->holeMask, applyParams))
            {
                tile->topologyDirty = true;
                tile->isDirty = true;
                tile->setAllLODsDirty();
                modifiedTiles.push_back(coord);

                // Track "after" state
                uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) << 32)
                              | static_cast<uint64_t>(static_cast<uint32_t>(coord.z));
                holeBrushAfterMasks[key] = tile->holeMask;
            }
        }

        if (!modifiedTiles.empty())
        {
            syncHoleBoundaries(grid, modifiedTiles);
            rebuildModifiedColliders(*targetEntity, grid, modifiedTiles);
        }

        events::holeBrush::HoleBrushAppliedNotification notification;
        notification.position = worldPosition;
        dispatcher.publish(notification);

        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(*targetEntity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).saveDirty = true;
            }
        }
    }

    void TerrainService::syncHoleBoundaries(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles)
    {
        uint32_t vertexCount = 0;
        const auto& allTiles = grid->getAllTiles();
        if (!allTiles.empty())
            vertexCount = allTiles[0]->config.getVertexCount();
        if (vertexCount == 0)
            return;

        uint32_t lastIdx = vertexCount - 1;

        for (const auto& coord : modifiedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile || !tile->hasHoleMask())
                continue;

            // Sync +X neighbor (tile's column lastIdx == neighbor's column 0)
            terrain::TerrainTile* neighborPX = grid->getTile({coord.x + 1, coord.z});
            if (neighborPX)
            {
                if (!neighborPX->hasHoleMask())
                    neighborPX->initializeHoleMask();

                bool changed = false;
                for (uint32_t z = 0; z < vertexCount; ++z)
                {
                    bool holeVal = tile->isHole(lastIdx, z);
                    if (neighborPX->isHole(0, z) != holeVal)
                    {
                        neighborPX->setHole(0, z, holeVal);
                        changed = true;
                    }
                }
                if (changed)
                {
                    neighborPX->topologyDirty = true;
                    neighborPX->isDirty = true;
                    neighborPX->setAllLODsDirty();
                }
            }

            // Sync -X neighbor (tile's column 0 == neighbor's column lastIdx)
            terrain::TerrainTile* neighborNX = grid->getTile({coord.x - 1, coord.z});
            if (neighborNX)
            {
                if (!neighborNX->hasHoleMask())
                    neighborNX->initializeHoleMask();

                bool changed = false;
                for (uint32_t z = 0; z < vertexCount; ++z)
                {
                    bool holeVal = tile->isHole(0, z);
                    if (neighborNX->isHole(lastIdx, z) != holeVal)
                    {
                        neighborNX->setHole(lastIdx, z, holeVal);
                        changed = true;
                    }
                }
                if (changed)
                {
                    neighborNX->topologyDirty = true;
                    neighborNX->isDirty = true;
                    neighborNX->setAllLODsDirty();
                }
            }

            // Sync +Z neighbor (tile's row lastIdx == neighbor's row 0)
            terrain::TerrainTile* neighborPZ = grid->getTile({coord.x, coord.z + 1});
            if (neighborPZ)
            {
                if (!neighborPZ->hasHoleMask())
                    neighborPZ->initializeHoleMask();

                bool changed = false;
                for (uint32_t x = 0; x < vertexCount; ++x)
                {
                    bool holeVal = tile->isHole(x, lastIdx);
                    if (neighborPZ->isHole(x, 0) != holeVal)
                    {
                        neighborPZ->setHole(x, 0, holeVal);
                        changed = true;
                    }
                }
                if (changed)
                {
                    neighborPZ->topologyDirty = true;
                    neighborPZ->isDirty = true;
                    neighborPZ->setAllLODsDirty();
                }
            }

            // Sync -Z neighbor (tile's row 0 == neighbor's row lastIdx)
            terrain::TerrainTile* neighborNZ = grid->getTile({coord.x, coord.z - 1});
            if (neighborNZ)
            {
                if (!neighborNZ->hasHoleMask())
                    neighborNZ->initializeHoleMask();

                bool changed = false;
                for (uint32_t x = 0; x < vertexCount; ++x)
                {
                    bool holeVal = tile->isHole(x, 0);
                    if (neighborNZ->isHole(x, lastIdx) != holeVal)
                    {
                        neighborNZ->setHole(x, lastIdx, holeVal);
                        changed = true;
                    }
                }
                if (changed)
                {
                    neighborNZ->topologyDirty = true;
                    neighborNZ->isDirty = true;
                    neighborNZ->setAllLODsDirty();
                }
            }
        }
    }

    void TerrainService::captureHoleMaskBefore(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& tiles)
    {
        for (const auto& coord : tiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
                continue;

            HoleMaskSnapshot snap;
            snap.coord = coord;
            snap.holeMask = tile->hasHoleMask() ? tile->holeMask : std::vector<uint8_t>();
            holeBrushBeforeSnapshots.push_back(std::move(snap));
        }
    }

    void TerrainService::finalizeHoleBrushUndo(terrain::TerrainGrid* grid)
    {
        if (!holeBrushDragActive || holeBrushAfterMasks.empty())
        {
            holeBrushDragActive = false;
            holeBrushBeforeSnapshots.clear();
            holeBrushAfterMasks.clear();
            return;
        }

        // Build after snapshots from tracked masks
        std::vector<HoleMaskSnapshot> afterSnapshots;
        for (const auto& snap : holeBrushBeforeSnapshots)
        {
            uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(snap.coord.x)) << 32)
                          | static_cast<uint64_t>(static_cast<uint32_t>(snap.coord.z));
            auto it = holeBrushAfterMasks.find(key);
            if (it != holeBrushAfterMasks.end())
            {
                HoleMaskSnapshot afterSnap;
                afterSnap.coord = snap.coord;
                afterSnap.holeMask = it->second;
                afterSnapshots.push_back(std::move(afterSnap));
            }
            else
            {
                // Tile was in before but not modified — get current state from grid
                terrain::TerrainTile* tile = grid->getTile(snap.coord);
                if (tile)
                {
                    HoleMaskSnapshot afterSnap;
                    afterSnap.coord = snap.coord;
                    afterSnap.holeMask = tile->hasHoleMask() ? tile->holeMask : std::vector<uint8_t>();
                    afterSnapshots.push_back(std::move(afterSnap));
                }
            }
        }

        auto undoCmd = std::make_unique<HoleBrushUndoCommand>(
            holeBrushUndoEntityId,
            std::move(holeBrushBeforeSnapshots),
            std::move(afterSnapshots),
            [this](const std::vector<HoleMaskSnapshot>& snapshots)
            {
                restoreHoleMasks(snapshots);
            });

        if (undoRedoService)
        {
            undoRedoService->pushCommand(std::move(undoCmd));
        }

        holeBrushDragActive = false;
        holeBrushBeforeSnapshots.clear();
        holeBrushAfterMasks.clear();
    }

    void TerrainService::restoreHoleMasks(const std::vector<HoleMaskSnapshot>& snapshots)
    {
        for (const auto& snap : snapshots)
        {
            // Find the grid that contains this tile
            for (auto& [entityId, grid] : terrainGrids)
            {
                terrain::TerrainTile* tile = grid->getTile(snap.coord);
                if (!tile)
                    continue;

                if (snap.holeMask.empty())
                {
                    tile->holeMask.clear();
                }
                else
                {
                    tile->holeMask = snap.holeMask;
                }
                tile->topologyDirty = true;
                tile->isDirty = true;
                tile->setAllLODsDirty();
                break;
            }
        }
    }

    uint16_t TerrainService::getOverlayMask() const
    {
        uint16_t overlayMask = 0;
        std::string materialPath = getTerrainMaterialPath();
        if (!materialPath.empty())
        {
            auto materialData = resource::ResourceManager::loadTerrainMaterial(materialPath);
            if (materialData)
            {
                for (uint8_t i = 0; i < materialData->activeLayerCount && i < 16; ++i)
                {
                    if (materialData->layers[i].blendMode == terrain::TerrainLayerBlendMode::Overlay)
                    {
                        overlayMask |= (1u << i);
                    }
                }
            }
        }
        return overlayMask;
    }
}
