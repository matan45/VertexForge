#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "TerrainAssetMetadata.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include <asset/AssetRef.hpp>
#include "threading/JobSystem.hpp"
#include <algorithm>

namespace services
{
    bool TerrainService::prepareSave(uint64_t terrainEntityId)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
            return false;

        auto cacheIt = fileCaches.find(terrainEntityId);
        if (cacheIt == fileCaches.end() || !cacheIt->second)
            return true;

        auto& grid = *gridIt->second;
        auto& generator = grid.getGenerator();
        auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
            return grid.getTile(coord);
        };

        // Stream in all previously-saved tiles from file cache that aren't in the grid
        // so they are included in the save (streaming may have unloaded them).
        // Uses getSavedCoords() to skip entries with no file data (newly added, never saved).
        // This MUST run on the main thread to avoid racing with the render thread.
        auto availableCoords = cacheIt->second->getSavedCoords();
        for (const auto& coord : availableCoords)
        {
            if (!grid.getTile(coord))
            {
                grid.addTileFromFile(coord);
            }
        }

        for (auto* tile : grid.getAllTiles())
        {
            if (tile && !tile->hasHeightData())
                cacheIt->second->ensureHeightsLoaded(*tile);
            if (tile && !tile->hasAnyLODData())
                cacheIt->second->ensureLODsLoaded(*tile, generator, getTile);
        }

        return true;
    }

    bool TerrainService::prepareSaveIncremental(uint64_t terrainEntityId)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
            return false;

        auto cacheIt = fileCaches.find(terrainEntityId);
        if (cacheIt == fileCaches.end() || !cacheIt->second)
            return prepareSave(terrainEntityId);

        auto& grid = *gridIt->second;
        auto& cache = *cacheIt->second;

        // Detect conditions that force a full save BEFORE the background thread starts,
        // because prepareSave() adds tiles to the grid (not thread-safe). Getting this wrong is
        // not merely a slow path: saveTerrainIncremental() falls back to saveTerrain() on the
        // background thread WITHOUT prepareSave(), and saveTerrain() writes only the tiles
        // currently resident in the grid — so with streaming on, every unloaded tile would be
        // silently dropped from the file.
        bool needsFullSave = cache.hasNewOrRemovedTiles();

        // Nothing to write incrementally means saveTerrainIncremental() will full-save too.
        if (cache.getDirtyCount() == 0)
            needsFullSave = true;

        // Settle an interrupted earlier save here, on the main thread, rather than letting the
        // background job discover it: replaying a journal moves the on-disk header and index, so
        // the cache has to be refreshed afterwards, and refreshing is only safe out here.
        const std::string& cachedPath = cache.getFilePath();
        if (!cachedPath.empty())
        {
            const auto recovery = terrain::TerrainSerializer::recoverPending(cachedPath);
            if (recovery == terrain::TerrainRecoveryResult::Failed)
            {
                vfLogError("TerrainService: Could not recover an interrupted save for {}", cachedPath);
                return false;
            }
            if (recovery != terrain::TerrainRecoveryResult::NotNeeded)
            {
                cache.refreshIndex(cachedPath);
                needsFullSave = true;
            }
        }

        // Matches the serializer's guard: a dirty tile with no index slot cannot be written
        // incrementally, and prepareSave() below is what makes the full save that replaces it safe.
        if (!needsFullSave)
        {
            const auto& indexMap = cache.getIndexMap();
            for (const auto& coord : cache.getDirtyCoords())
            {
                if (indexMap.find(coord) == indexMap.end())
                {
                    needsFullSave = true;
                    break;
                }
            }
        }

        // Check whether the header would change size (material path length, or the optional
        // physics/streaming blocks toggling), which moves the index table.
        // NOTE: TerrainSerializer::saveIncremental() has a matching guard as a safety net, built
        // from the same terrain::serializedHeaderSize(). Both must agree — if updating one,
        // update the other. This one may be more eager, never less.
        if (!needsFullSave)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});

            bool hasPhysicsNow = false;
            if (registry.valid(ent) && registry.all_of<components::TerrainColliderComponent>(ent))
                hasPhysicsNow = registry.get<components::TerrainColliderComponent>(ent).hasCollider;

            bool hasStreamingNow = false;
            auto streamerIt = worldStreamers.find(terrainEntityId);
            if (streamerIt != worldStreamers.end() && streamerIt->second)
                hasStreamingNow = streamerIt->second->isEnabled();

            // Mirror what TerrainSerializer::computeFlags() would derive from the same configs.
            // Only these two flags affect header size, so the rest can stay as they were saved.
            terrain::TerrainFileHeader candidate = cache.getHeader();
            auto flagBits = static_cast<uint32_t>(candidate.flags);
            constexpr auto physicsBit = static_cast<uint32_t>(terrain::TerrainFormatFlags::HAS_PHYSICS_DATA);
            constexpr auto streamingBit = static_cast<uint32_t>(terrain::TerrainFormatFlags::HAS_STREAMING_CONFIG);
            flagBits = hasPhysicsNow ? (flagBits | physicsBit) : (flagBits & ~physicsBit);
            flagBits = hasStreamingNow ? (flagBits | streamingBit) : (flagBits & ~streamingBit);
            candidate.flags = static_cast<terrain::TerrainFormatFlags>(flagBits);

            // Resolving here also warms the AssetRef path cache on the main thread, so the
            // background save reads back the identical string.
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
                candidate.materialPath = registry.get<components::TerrainComponent>(ent).terrainMaterialRef.resolve();

            if (terrain::serializedHeaderSize(candidate) != cache.getIndexTableOffset())
                needsFullSave = true;
        }

        if (needsFullSave)
        {
            vfLogInfo("TerrainService: Incremental save not possible, preparing full save");
            return prepareSave(terrainEntityId);
        }

        auto& generator = grid.getGenerator();
        auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
            return grid.getTile(coord);
        };

        // Only ensure dirty tiles have heights + LODs loaded
        for (const auto& coord : cache.getDirtyCoords())
        {
            auto* tile = grid.getTile(coord);
            if (!tile) continue;

            if (!tile->hasHeightData())
                cache.ensureHeightsLoaded(*tile);
            if (!tile->hasAnyLODData())
                cache.ensureLODsLoaded(*tile, generator, getTile);
        }

        return true;
    }

    // VK-1645: VFTR stores the DERIVED composite, which is correct -- it is the flattened runtime
    // artifact. But the authoritative bases and the layer stack are RAM-only until the VK-1646
    // sidecar lands, so a save-and-reload bakes the corridor and loses both. Say so out loud
    // rather than let an artist discover it after closing the editor.
    //
    // VFTR flag bit 6 (HAS_EDIT_LAYER_SIDECAR) is deliberately NOT set here: nothing writes a
    // sidecar yet, and the documented load rule is "marker set + sidecar missing => disable layer
    // editing and offer recovery", which would drop every terrain saved by this build straight
    // into VK-1646's recovery path.
    void TerrainService::warnUnpersistedHeightLayers(const terrain::TerrainGrid& grid)
    {
        const terrain::TerrainHeightLayerStore& store = grid.getHeightLayers();
        if (store.empty())
            return;

        vfLogWarning("TerrainService: {} reserved height layer(s) over {} authoritative base "
                     "block(s) are NOT persisted yet (VK-1646). The saved terrain keeps the "
                     "composited heights; the layer stack is lost on reload.",
                     store.layers().size(), store.baseCount());
    }

    bool TerrainService::saveTerrainIncremental(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        auto cacheIt = fileCaches.find(terrainEntityId);
        if (cacheIt == fileCaches.end() || !cacheIt->second)
        {
            vfLogInfo("TerrainService: No file cache, falling back to full save");
            return saveTerrain(terrainEntityId, path); // warns about layers on its own path
        }

        warnUnpersistedHeightLayers(*gridIt->second);

        auto& cache = *cacheIt->second;

        // If tiles were added/removed, tile count changed → fall back to full save
        // Note: prepareSaveIncremental() on the main thread should have already detected this
        // and called prepareSave(). We only call saveTerrain() here (no prepareSave — unsafe on bg thread).
        if (cache.hasNewOrRemovedTiles())
        {
            vfLogInfo("TerrainService: Tiles added/removed, falling back to full save");
            return saveTerrain(terrainEntityId, path);
        }

        if (cache.getDirtyCount() == 0)
        {
            // No dirty tiles, but config changes (material path, streaming/physics) still need
            // persisting. prepareSaveIncremental() forces prepareSave() for this case, so the
            // grid is fully resident and saveTerrain() cannot drop streamed-out tiles here.
            vfLogInfo("TerrainService: No dirty tiles, falling back to full save for config changes");
            return saveTerrain(terrainEntityId, path);
        }

        // Gather physics and streaming config from components
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});

        if (!registry.valid(ent) || !registry.all_of<components::TerrainComponent>(ent))
        {
            vfLogError("TerrainService: Entity {} has no TerrainComponent", terrainEntityId);
            return false;
        }

        terrain::TerrainPhysicsConfig physicsConfig;
        if (registry.all_of<components::TerrainColliderComponent>(ent))
        {
            const auto& cc = registry.get<components::TerrainColliderComponent>(ent);
            physicsConfig.hasCollider = cc.hasCollider;
            physicsConfig.collisionLayer = cc.collisionLayer;
            physicsConfig.friction = cc.friction;
            physicsConfig.restitution = cc.restitution;
        }

        terrain::TerrainStreamingConfig streamingConfig;
        auto streamerIt = worldStreamers.find(terrainEntityId);
        if (streamerIt != worldStreamers.end() && streamerIt->second)
        {
            streamingConfig.enabled = streamerIt->second->isEnabled();
            const auto& cfg = streamerIt->second->getConfig();
            streamingConfig.loadRadius = cfg.loadRadius;
            streamingConfig.unloadRadius = cfg.unloadRadius;
            streamingConfig.maxLoadsPerFrame = cfg.maxLoadsPerFrame;
            streamingConfig.maxUnloadsPerFrame = cfg.maxUnloadsPerFrame;
        }

        auto& comp = registry.get<components::TerrainComponent>(ent);

        terrain::TerrainIncrementalSaveParams incParams;
        incParams.path = path;
        incParams.grid = gridIt->second.get();
        incParams.dirtyCoords = &cache.getDirtyCoords();
        incParams.currentHeader = cache.getHeader();
        incParams.indexTableOffset = cache.getIndexTableOffset();
        incParams.currentIndexMap = &cache.getIndexMap();
        // Same source as the full save — the live component, not the stale on-disk header.
        incParams.materialPath = comp.terrainMaterialRef.resolve();
        incParams.physicsConfig = physicsConfig;
        incParams.streamingConfig = streamingConfig;

        const auto result = terrain::TerrainSerializer::saveIncremental(incParams);

        if (result == terrain::TerrainIncrementalSaveResult::NeedsFullSave)
        {
            // Only this outcome is safe to answer with a full save. prepareSaveIncremental() checks
            // the same conditions on the main thread and calls prepareSave() for them, so the grid
            // is resident and saveTerrain() cannot drop a streamed-out tile.
            vfLogWarning("TerrainService: Incremental save not applicable, falling back to full save");
            return saveTerrain(terrainEntityId, path);
        }

        if (result == terrain::TerrainIncrementalSaveResult::Failed)
        {
            // A real IO failure. Falling back here would run saveTerrain() without prepareSave(),
            // which writes only the resident tiles — with streaming on that deletes every unloaded
            // tile from the file. Report the failure and leave the file as it is instead.
            vfLogError("TerrainService: Incremental save failed for {}; the file is unchanged", path);
            return false;
        }

        // Post-save bookkeeping
        size_t savedCount = cache.getDirtyCount();

        saveVegetation(terrainEntityId, path);
        saveFoliage(terrainEntityId, path);

        comp.saveDirty = false;

        // Reclaim the records superseded by this and earlier incremental saves, once enough of them
        // have piled up. Deliberately after the commit succeeded and deliberately unable to fail
        // the save: compaction is atomic, so a failure leaves a valid — merely fat — file, and
        // routing that into the full-save fallback would turn a space optimisation into data loss.
        // It must also run before refreshIndex(), which is what picks up the relocated offsets.
        bool compacted = false;
        if (!terrain::TerrainSerializer::compactIfNeeded(path, &compacted))
            vfLogWarning("TerrainService: Compaction of {} did not run; the file remains valid", path);
        else if (compacted)
            vfLogInfo("TerrainService: Compacted {}", path);

        cache.refreshIndex(path);

        // Same metadata contract as the full save — see refreshTerrainSidecar().
        const auto metaGuid = refreshTerrainSidecar(path);
        if (metaGuid.isValid())
            comp.terrainRef = asset::AssetRef::fromGUIDAndPath(metaGuid, path);

        events::terrain::TerrainSavedNotification savedNotification;
        savedNotification.terrainEntity = EntityHandle{terrainEntityId};
        savedNotification.path = path;
        events::EventDispatcher::instance().publish(savedNotification);

        vfLogInfo("TerrainService: Incremental save completed ({} tiles updated)", savedCount);
        return true;
    }

    bool TerrainService::saveTerrain(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        warnUnpersistedHeightLayers(*gridIt->second);

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});

        if (!registry.valid(ent) || !registry.all_of<components::TerrainComponent>(ent))
        {
            vfLogError("TerrainService: Entity {} has no TerrainComponent", terrainEntityId);
            return false;
        }

        const auto& comp = registry.get<components::TerrainComponent>(ent);

        terrain::TerrainTileConfig tileConfig;
        switch (comp.resolution)
        {
        case 0: tileConfig.resolution = terrain::TileResolution::Low; break;
        case 1: tileConfig.resolution = terrain::TileResolution::Medium; break;
        case 2: tileConfig.resolution = terrain::TileResolution::High; break;
        default: tileConfig.resolution = terrain::TileResolution::Low; break;
        }
        tileConfig.worldTileSize = comp.worldTileSize;
        tileConfig.maxHeight = comp.maxHeight;
        tileConfig.minHeight = comp.minHeight;

        terrain::TerrainPhysicsConfig physicsConfig;
        if (registry.all_of<components::TerrainColliderComponent>(ent))
        {
            const auto& cc = registry.get<components::TerrainColliderComponent>(ent);
            physicsConfig.hasCollider = cc.hasCollider;
            physicsConfig.collisionLayer = cc.collisionLayer;
            physicsConfig.friction = cc.friction;
            physicsConfig.restitution = cc.restitution;
        }

        terrain::TerrainStreamingConfig streamingConfig;
        auto streamerIt = worldStreamers.find(terrainEntityId);
        if (streamerIt != worldStreamers.end() && streamerIt->second)
        {
            streamingConfig.enabled = streamerIt->second->isEnabled();
            const auto& cfg = streamerIt->second->getConfig();
            streamingConfig.loadRadius = cfg.loadRadius;
            streamingConfig.unloadRadius = cfg.unloadRadius;
            streamingConfig.maxLoadsPerFrame = cfg.maxLoadsPerFrame;
            streamingConfig.maxUnloadsPerFrame = cfg.maxUnloadsPerFrame;
        }

        // Compute actual bounds from grid tiles (supports sparse/dynamic grids)
        int32_t boundsMinX, boundsMinZ, boundsMaxX, boundsMaxZ;
        gridIt->second->computeBounds(boundsMinX, boundsMinZ, boundsMaxX, boundsMaxZ);

        terrain::TerrainSaveParams saveParams;
        saveParams.path = path;
        saveParams.grid = gridIt->second.get();
        saveParams.config = tileConfig;
        saveParams.gridMinX = boundsMinX;
        saveParams.gridMinZ = boundsMinZ;
        saveParams.gridMaxX = boundsMaxX;
        saveParams.gridMaxZ = boundsMaxZ;
        saveParams.materialPath = comp.terrainMaterialRef.resolve();
        saveParams.physicsConfig = physicsConfig;
        saveParams.streamingConfig = streamingConfig;

        bool result = terrain::TerrainSerializer::save(saveParams);

        if (result)
        {
            saveVegetation(terrainEntityId, path);
            saveFoliage(terrainEntityId, path);

            auto& mutableComp = registry.get<components::TerrainComponent>(ent);
            mutableComp.savePath = path;
            mutableComp.saveDirty = false;
            mutableComp.gridMinX = boundsMinX;
            mutableComp.gridMinZ = boundsMinZ;
            mutableComp.gridMaxX = boundsMaxX;
            mutableComp.gridMaxZ = boundsMaxZ;
            mutableComp.activeTileCount = static_cast<uint32_t>(gridIt->second->getTileCount());

            auto cacheIt = fileCaches.find(terrainEntityId);
            if (cacheIt != fileCaches.end() && cacheIt->second)
            {
                cacheIt->second->refreshIndex(path);
            }
            else
            {
                terrain::TerrainFileHeader newHeader;
                std::vector<terrain::TileIndexEntry> newIndex;
                uint64_t newIndexOffset = 0;
                if (terrain::TerrainSerializer::readHeader(path, newHeader, newIndex, &newIndexOffset))
                {
                    auto cache = std::make_shared<terrain::TerrainFileCache>(path, newHeader, newIndex, newIndexOffset);
                    fileCaches[terrainEntityId] = cache;
                    gridIt->second->setFileCache(cache);
                }
            }

            // Create or update .vfmeta sidecar
            const auto metaGuid = refreshTerrainSidecar(path);
            if (metaGuid.isValid())
                mutableComp.terrainRef = asset::AssetRef::fromGUIDAndPath(metaGuid, path);

            events::terrain::TerrainSavedNotification savedNotification;
            savedNotification.terrainEntity = EntityHandle{terrainEntityId};
            savedNotification.path = path;
            events::EventDispatcher::instance().publish(savedNotification);

            vfLogInfo("TerrainService: Saved terrain to {}", path);
        }

        return result;
    }

    EntityHandle TerrainService::loadTerrain(const std::string& path, const asset::AssetRef& terrainRef)
    {
        terrain::TerrainFileHeader header;
        std::vector<terrain::TileIndexEntry> index;
        uint64_t indexTableOffset = 0;

        // A save interrupted by a crash leaves its commit in a journal beside the file. Settle it
        // before anything reads the header, or the header and index may still be the spliced
        // halves the crash left behind.
        if (terrain::TerrainSerializer::recoverPending(path) == terrain::TerrainRecoveryResult::Failed)
        {
            vfLogError("TerrainService: Could not recover an interrupted save for {}", path);
            return {};
        }

        if (!terrain::TerrainSerializer::readHeader(path, header, index, &indexTableOffset))
        {
            vfLogError("TerrainService: Failed to read terrain header from {}", path);
            return {};
        }

        return finishLoadTerrain(header, index, path, indexTableOffset, terrainRef);
    }

    void TerrainService::loadInitialTiles(
        terrain::TerrainGrid& grid,
        const terrain::TerrainFileHeader& header,
        const std::vector<terrain::TileIndexEntry>& index)
    {
        if (header.streamingConfig.enabled)
        {
            // Distance measured from world origin (0,0). The streamer corrects
            // to actual camera position on the first frame after load.
            float loadRadiusSq = header.streamingConfig.loadRadius * header.streamingConfig.loadRadius;
            float tileSize = header.worldTileSize;

            for (const auto& entry : index)
            {
                float cx = (static_cast<float>(entry.coordX) + 0.5f) * tileSize;
                float cz = (static_cast<float>(entry.coordZ) + 0.5f) * tileSize;
                if (cx * cx + cz * cz <= loadRadiusSq)
                    grid.addTileFromFile(terrain::TileCoord{entry.coordX, entry.coordZ});
            }
        }
        else
        {
            grid.loadMetadataOnly(header, index);
        }
    }

    void TerrainService::initTerrainComponent(
        components::TerrainComponent& comp,
        const terrain::TerrainFileHeader& header,
        const std::string& path,
        uint32_t activeTileCount,
        const asset::AssetRef& terrainRef)
    {
        comp.resolution = header.resolution;
        comp.worldTileSize = header.worldTileSize;
        comp.maxHeight = header.maxHeight;
        comp.minHeight = header.minHeight;
        comp.gridMinX = header.gridMinX;
        comp.gridMinZ = header.gridMinZ;
        comp.gridMaxX = header.gridMaxX;
        comp.gridMaxZ = header.gridMaxZ;
        comp.terrainMaterialRef = asset::AssetRef::fromPath(header.materialPath);
        comp.isActive = true;
        comp.isDirty = false;
        comp.activeTileCount = activeTileCount;
        comp.visibleTileCount = 0;
        comp.terrainRef = terrainRef.isValid() ? terrainRef : asset::AssetRef::fromPath(path);
        comp.savePath = path;
        comp.saveDirty = false;
    }

    void TerrainService::publishTerrainCreated(EntityHandle handle, const terrain::TerrainFileHeader& header)
    {
        events::terrain::TerrainCreatedNotification notification;
        notification.terrainEntity = handle;
        notification.config.resolution = header.resolution;
        notification.config.worldTileSize = header.worldTileSize;
        notification.config.maxHeight = header.maxHeight;
        notification.config.minHeight = header.minHeight;
        notification.config.terrainMaterialPath = header.materialPath;
        notification.config.tilesX = header.gridMaxX - header.gridMinX + 1;
        notification.config.tilesZ = header.gridMaxZ - header.gridMinZ + 1;
        events::EventDispatcher::instance().publish(notification);
    }

    EntityHandle TerrainService::finishLoadTerrain(
        terrain::TerrainFileHeader& header,
        std::vector<terrain::TileIndexEntry>& index,
        const std::string& path,
        uint64_t indexTableOffset,
        const asset::AssetRef& terrainRef)
    {
        terrain::TerrainTileConfig tileConfig;
        tileConfig.resolution = static_cast<terrain::TileResolution>(header.resolution);
        tileConfig.worldTileSize = header.worldTileSize;
        tileConfig.maxHeight = header.maxHeight;
        tileConfig.minHeight = header.minHeight;
        tileConfig.skirtDepth = header.skirtDepth;

        auto grid = std::make_unique<terrain::TerrainGrid>(tileConfig);
        auto cache = std::make_shared<terrain::TerrainFileCache>(path, header, index, indexTableOffset);
        grid->setFileCache(cache);
        loadInitialTiles(*grid, header, index);

        scene::Entity parentEntity("Terrain");
        sceneGraph->addChild(sceneGraph->GetRoot(), parentEntity);

        auto& terrainComp = parentEntity.addComponent<components::TerrainComponent>();
        initTerrainComponent(terrainComp, header, path, static_cast<uint32_t>(grid->getTileCount()),
                             terrainRef);

        EntityHandle parentHandle = internal::toHandle(parentEntity.getHandle());
        createTileEntities(parentHandle, *grid);

        terrainGrids[parentHandle.id] = std::move(grid);
        fileCaches[parentHandle.id] = cache;

        if (!worldModeActive)
        {
            terrain::StreamingConfig stCfg{
                header.streamingConfig.loadRadius, header.streamingConfig.unloadRadius,
                header.streamingConfig.maxLoadsPerFrame, header.streamingConfig.maxUnloadsPerFrame};
            auto streamer = std::make_unique<terrain::TerrainWorldStreamer>(stCfg);
            streamer->setEnabled(header.streamingConfig.enabled);
            worldStreamers[parentHandle.id] = std::move(streamer);
        }

        if (!header.materialPath.empty())
            syncWeightMapLayerCount(parentHandle.id, header.materialPath);

        publishTerrainCreated(parentHandle, header);

        if (header.physicsConfig.hasCollider && physicsProvider)
        {
            auto& cc = parentEntity.addComponent<components::TerrainColliderComponent>();
            cc.collisionLayer = header.physicsConfig.collisionLayer;
            cc.friction = header.physicsConfig.friction;
            cc.restitution = header.physicsConfig.restitution;
            addTerrainCollider(parentHandle);
        }

        if (header.streamingConfig.enabled)
            vfLogInfo("TerrainService: Loaded terrain with {}/{} initial tiles (streaming) from {}",
                      terrainComp.activeTileCount, header.tileCount, path);
        else
            vfLogInfo("TerrainService: Loaded terrain with {} tiles from {}", header.tileCount, path);

        loadVegetation(parentHandle.id, path);
        loadFoliage(parentHandle.id, path);

        // If world mode is already active with loaded sectors, activate their terrain tiles
        activateTilesForLoadedSectors();

        return parentHandle;
    }

}
