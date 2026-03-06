#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/HeightmapLoader.hpp"
#include "resource/ResourceManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainEvents.hpp"

namespace services
{
    EntityHandle TerrainService::createTerrain(const TerrainCreationData& config)
    {
        terrain::TerrainTileConfig tileConfig;

        switch (config.resolution)
        {
        case 0: tileConfig.resolution = terrain::TileResolution::Low; break;
        case 1: tileConfig.resolution = terrain::TileResolution::Medium; break;
        case 2: tileConfig.resolution = terrain::TileResolution::High; break;
        default: tileConfig.resolution = terrain::TileResolution::Low; break;
        }

        tileConfig.worldTileSize = config.worldTileSize;
        tileConfig.maxHeight = config.maxHeight;
        tileConfig.minHeight = config.minHeight;

        for (int i = 0; i < 4; ++i)
        {
            tileConfig.lodDistances[i] = config.lodDistances[i];
        }

        int32_t halfX = config.tilesX / 2;
        int32_t halfZ = config.tilesZ / 2;
        int32_t minX = -halfX;
        int32_t minZ = -halfZ;
        int32_t maxX = config.tilesX - halfX - 1;
        int32_t maxZ = config.tilesZ - halfZ - 1;

        auto grid = std::make_unique<terrain::TerrainGrid>(tileConfig);

        if (config.heightmapPath.empty())
        {
            grid->setHeightSampler([](float /*worldX*/, float /*worldZ*/) -> float {
                return 0.0f;
            });
        }
        else
        {
            auto heightmapData = terrain::HeightmapLoader::load(config.heightmapPath);
            if (heightmapData && heightmapData->isValid())
            {
                float terrainMinX = static_cast<float>(minX) * config.worldTileSize;
                float terrainMinZ = static_cast<float>(minZ) * config.worldTileSize;
                float terrainWidth = static_cast<float>(config.tilesX) * config.worldTileSize;
                float terrainDepth = static_cast<float>(config.tilesZ) * config.worldTileSize;

                grid->setHeightSampler(terrain::createHeightSamplerFromMap(
                    heightmapData,
                    terrainMinX,
                    terrainMinZ,
                    terrainWidth,
                    terrainDepth,
                    config.minHeight,
                    config.maxHeight
                ));

                vfLogInfo("Loaded heightmap from: {}", config.heightmapPath);
            }
            else
            {
                vfLogWarning("Failed to load heightmap: {}, creating flat terrain", config.heightmapPath);
                grid->setHeightSampler([](float /*worldX*/, float /*worldZ*/) -> float {
                    return 0.0f;
                });
            }
        }

        grid->createGrid(minX, minZ, maxX, maxZ, nullptr);

        scene::Entity parentEntity("Terrain");
        sceneGraph->addChild(sceneGraph->GetRoot(), parentEntity);

        auto& terrainComp = parentEntity.addComponent<components::TerrainComponent>();
        terrainComp.resolution = config.resolution;
        terrainComp.worldTileSize = config.worldTileSize;
        terrainComp.maxHeight = config.maxHeight;
        terrainComp.minHeight = config.minHeight;
        terrainComp.gridMinX = minX;
        terrainComp.gridMinZ = minZ;
        terrainComp.gridMaxX = maxX;
        terrainComp.gridMaxZ = maxZ;
        terrainComp.lodDistances = config.lodDistances;
        terrainComp.heightmapPath = config.heightmapPath;
        terrainComp.terrainMaterialPath = config.terrainMaterialPath;
        terrainComp.weightMapPath = config.weightMapPath;
        terrainComp.isActive = true;
        terrainComp.isDirty = false;
        terrainComp.activeTileCount = static_cast<uint32_t>(config.tilesX * config.tilesZ);
        terrainComp.visibleTileCount = 0;

        EntityHandle parentHandle = internal::toHandle(parentEntity.getHandle());

        createTileEntities(parentHandle, *grid);

        terrainGrids[parentHandle.id] = std::move(grid);
        worldStreamers[parentHandle.id] = std::make_unique<terrain::TerrainWorldStreamer>();

        if (!config.weightMapPath.empty())
        {
            loadWeightMaps(parentHandle.id, config.weightMapPath);
        }

        if (!config.terrainMaterialPath.empty())
        {
            syncWeightMapLayerCount(parentHandle.id, config.terrainMaterialPath);
        }

        events::terrain::TerrainCreatedNotification notification;
        notification.terrainEntity = parentHandle;
        notification.config = config;
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("Created terrain with {} tiles", config.tilesX * config.tilesZ);

        return parentHandle;
    }

    void TerrainService::createTileEntities(EntityHandle parentHandle, terrain::TerrainGrid& grid)
    {
        for (auto* tile : grid.getAllTiles())
        {
            createTileEntity(parentHandle, tile, tile->coord.x, tile->coord.z);
        }
    }

    bool TerrainService::deleteTerrain(EntityHandle terrainEntity)
    {
        if (!terrainEntity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity entity = internal::fromHandle(terrainEntity);

        if (!registry.valid(entity))
            return false;

        if (!registry.all_of<components::TerrainComponent>(entity))
            return false;

        const auto& comp = registry.get<components::TerrainComponent>(entity);
        if (!comp.terrainMaterialPath.empty())
            resource::ResourceManager::invalidateTerrainMaterialCache(comp.terrainMaterialPath);

        if (physicsProvider)
            physicsProvider->removeTerrainCollider(terrainEntity);

        terrainGrids.erase(terrainEntity.id);
        fileCaches.erase(terrainEntity.id);
        worldStreamers.erase(terrainEntity.id);

        scene::Entity terrainEnt(entity);
        sceneGraph->removeEntity(terrainEnt);

        events::terrain::TerrainDeletedNotification notification;
        notification.terrainEntity = terrainEntity;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    void TerrainService::remapTerrainEntities()
    {
        if (terrainGrids.empty())
            return;

        std::vector<bool> hadCollider;
        std::vector<std::unique_ptr<terrain::TerrainGrid>> grids;
        std::vector<std::shared_ptr<terrain::TerrainFileCache>> caches;
        std::vector<std::unique_ptr<terrain::TerrainWorldStreamer>> streamers;
        for (auto& [id, grid] : terrainGrids)
        {
            bool hasCollider = physicsProvider && physicsProvider->hasTerrainCollider(EntityHandle{id});
            hadCollider.push_back(hasCollider);

            if (hasCollider)
                physicsProvider->removeTerrainCollider(EntityHandle{id});

            grids.push_back(std::move(grid));
            auto cacheIt = fileCaches.find(id);
            caches.push_back(cacheIt != fileCaches.end() ? std::move(cacheIt->second) : nullptr);
            auto streamerIt = worldStreamers.find(id);
            streamers.push_back(streamerIt != worldStreamers.end() ? std::move(streamerIt->second) : nullptr);
        }
        terrainGrids.clear();
        fileCaches.clear();
        worldStreamers.clear();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::TerrainComponent>();

        size_t gridIndex = 0;
        for (auto entity : view)
        {
            if (gridIndex >= grids.size())
                break;

            uint64_t newId = internal::toHandle(entity).id;
            terrainGrids[newId] = std::move(grids[gridIndex]);
            if (caches[gridIndex])
                fileCaches[newId] = std::move(caches[gridIndex]);
            if (streamers[gridIndex])
                worldStreamers[newId] = std::move(streamers[gridIndex]);

            if (gridIndex < hadCollider.size() && hadCollider[gridIndex])
                addTerrainCollider(EntityHandle{newId});

            gridIndex++;
        }
    }

    std::vector<terrain::TerrainTile*> TerrainService::getRawVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<terrain::TerrainTile*> result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            // Run world streaming before LOD updates (skip during save to avoid data races)
            auto streamerIt = worldStreamers.find(entityId);
            if (streamerIt != worldStreamers.end() && streamerIt->second && streamerIt->second->isEnabled()
                && !saveInProgress.load(std::memory_order_acquire))
            {
                auto cacheIt = fileCaches.find(entityId);
                if (cacheIt != fileCaches.end() && cacheIt->second)
                {
                    EntityHandle terrainHandle{entityId};
                    auto& comp = scene::EntityRegistry::getRegistry()
                        .get<components::TerrainComponent>(internal::fromHandle(terrainHandle));

                    streamerIt->second->update(
                        cameraPosition, comp.worldTileSize, *cacheIt->second, *grid, streamingActions);

                    for (const auto& action : streamingActions)
                    {
                        if (action.isLoad)
                            streamInTile(terrainHandle, action.coord.x, action.coord.z);
                        else
                            streamOutTile(terrainHandle, action.coord.x, action.coord.z);
                    }

                    if (!streamingActions.empty())
                        commitStreamingChanges(terrainHandle);
                }
            }

            (void)grid->updateLODs(cameraPosition);

            grid->regenerateDirtyTiles(cameraPosition);

            auto visibleTiles = grid->getVisibleTiles(frustum);

            if (distanceCullingEnabled_ && maxTerrainDistSq_ > 0.0f)
            {
                for (terrain::TerrainTile* tile : visibleTiles)
                {
                    if (!tile || !tile->isVisible)
                        continue;

                    glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
                    glm::vec3 diff = tileCenter - cameraPosition;
                    float distSq = glm::dot(diff, diff);
                    if (distSq <= maxTerrainDistSq_)
                    {
                        result.push_back(tile);
                    }
                }
            }
            else
            {
                for (terrain::TerrainTile* tile : visibleTiles)
                {
                    if (tile && tile->isVisible)
                    {
                        result.push_back(tile);
                    }
                }
            }
        }

        return result;
    }

    std::vector<terrain::TerrainTile*> TerrainService::queryVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<terrain::TerrainTile*> result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            for (auto* tile : grid->getAllTiles())
            {
                if (!tile)
                    continue;

                if (!frustum.intersectsAABB(tile->worldBounds))
                    continue;

                if (distanceCullingEnabled_ && maxTerrainDistSq_ > 0.0f)
                {
                    glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
                    glm::vec3 diff = tileCenter - cameraPosition;
                    float distSq = glm::dot(diff, diff);
                    if (distSq > maxTerrainDistSq_)
                        continue;
                }

                result.push_back(tile);
            }
        }

        return result;
    }

    bool TerrainService::ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel)
    {
        for (auto& [entityId, grid] : terrainGrids)
        {
            if (!grid->getTile(tile.coord))
                continue;

            auto cacheIt = fileCaches.find(entityId);
            if (cacheIt == fileCaches.end() || !cacheIt->second)
                return false;

            auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
                return grid->getTile(coord);
            };

            return cacheIt->second->ensureLODsLoaded(tile, grid->getGenerator(), getTile);
        }

        return false;
    }

    void TerrainService::releaseTileRAMData(terrain::TerrainTile& tile)
    {
        for (auto& [entityId, grid] : terrainGrids)
        {
            if (!grid->getTile(tile.coord))
                continue;

            auto cacheIt = fileCaches.find(entityId);
            if (cacheIt != fileCaches.end() && cacheIt->second)
            {
                cacheIt->second->evictTileGeometry(tile);
            }
            return;
        }
    }

    void TerrainService::createTileEntity(EntityHandle parentHandle, terrain::TerrainTile* tile,
                                            int32_t tileX, int32_t tileZ)
    {
        scene::Entity parentEntity(internal::fromHandle(parentHandle));
        std::string tileName = "Tile_" + std::to_string(tileX) + "_" + std::to_string(tileZ);
        scene::Entity tileEntity(tileName);
        parentEntity.addChildren(tileEntity);

        auto& tileComp = tileEntity.addComponent<components::TerrainTileComponent>();
        tileComp.tileX = tileX;
        tileComp.tileZ = tileZ;
        tileComp.currentLOD = tile->currentLOD;
        tileComp.isVisible = tile->isVisible;
        tileComp.isDirty = tile->isDirty;
        tileComp.isGPUResident = false;
        tileComp.boundingMinY = tile->worldBounds.min.y;
        tileComp.boundingMaxY = tile->worldBounds.max.y;

        auto& transform = tileEntity.getComponent<components::TransformComponent>();
        transform.position = tile->worldOrigin;
        transform.isDirty = true;
    }

    bool TerrainService::addTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ)
    {
        if (!terrainEntity.isValid())
            return false;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return false;

        auto& grid = *gridIt->second;
        terrain::TileCoord coord{tileX, tileZ};

        // Check if tile already exists
        if (grid.getTile(coord))
            return false;

        terrain::TerrainTile* tile = grid.addTile(coord);
        if (!tile)
            return false;

        // Track in file cache if one exists
        auto cacheIt = fileCaches.find(terrainEntity.id);
        if (cacheIt != fileCaches.end() && cacheIt->second)
        {
            cacheIt->second->addNewTileEntry(coord);
        }

        createTileEntity(terrainEntity, tile, tileX, tileZ);

        // Update TerrainComponent bounds
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(terrainEntity);
        if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
        {
            auto& comp = registry.get<components::TerrainComponent>(ent);
            int32_t minX, minZ, maxX, maxZ;
            grid.computeBounds(minX, minZ, maxX, maxZ);
            comp.gridMinX = minX;
            comp.gridMinZ = minZ;
            comp.gridMaxX = maxX;
            comp.gridMaxZ = maxZ;
            comp.activeTileCount = static_cast<uint32_t>(grid.getTileCount());
            comp.saveDirty = true;
        }

        events::terrain::TerrainTileAddedNotification notification;
        notification.terrainEntity = terrainEntity;
        notification.tileX = tileX;
        notification.tileZ = tileZ;
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("TerrainService: Added tile ({}, {})", tileX, tileZ);
        return true;
    }

    bool TerrainService::removeTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ)
    {
        if (!terrainEntity.isValid())
            return false;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return false;

        auto& grid = *gridIt->second;
        terrain::TileCoord coord{tileX, tileZ};

        if (!grid.getTile(coord))
            return false;

        // Remove from grid (clears neighbor refs, marks neighbors dirty)
        grid.removeTile(coord);

        // Remove from file cache
        auto cacheIt = fileCaches.find(terrainEntity.id);
        if (cacheIt != fileCaches.end() && cacheIt->second)
        {
            cacheIt->second->removeEntry(coord);
        }

        // Find and destroy the child tile entity
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity parentEnt = internal::fromHandle(terrainEntity);
        if (registry.valid(parentEnt) && registry.all_of<components::ChildrenComponent>(parentEnt))
        {
            const auto& children = registry.get<components::ChildrenComponent>(parentEnt).children;
            for (auto childEnt : children)
            {
                if (!registry.valid(childEnt) ||
                    !registry.all_of<components::TerrainTileComponent>(childEnt))
                    continue;

                const auto& tc = registry.get<components::TerrainTileComponent>(childEnt);
                if (tc.tileX == tileX && tc.tileZ == tileZ)
                {
                    scene::Entity tileEntity(childEnt);
                    sceneGraph->removeEntity(tileEntity);
                    break;
                }
            }
        }

        // Update TerrainComponent bounds
        if (registry.valid(parentEnt) && registry.all_of<components::TerrainComponent>(parentEnt))
        {
            auto& comp = registry.get<components::TerrainComponent>(parentEnt);
            int32_t minX, minZ, maxX, maxZ;
            grid.computeBounds(minX, minZ, maxX, maxZ);
            comp.gridMinX = minX;
            comp.gridMinZ = minZ;
            comp.gridMaxX = maxX;
            comp.gridMaxZ = maxZ;
            comp.activeTileCount = static_cast<uint32_t>(grid.getTileCount());
            comp.saveDirty = true;
        }

        events::terrain::TerrainTileRemovedNotification notification;
        notification.terrainEntity = terrainEntity;
        notification.tileX = tileX;
        notification.tileZ = tileZ;
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("TerrainService: Removed tile ({}, {})", tileX, tileZ);
        return true;
    }

    bool TerrainService::streamInTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ)
    {
        if (!terrainEntity.isValid())
            return false;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return false;

        auto& grid = *gridIt->second;
        terrain::TileCoord coord{tileX, tileZ};

        if (grid.getTile(coord))
            return false;

        terrain::TerrainTile* tile = grid.addTileFromFile(coord);
        if (!tile)
            return false;

        // Note: saveDirty is intentionally NOT set here. Streamed-in tiles are
        // transient copies loaded from an already-saved file, not user modifications.

        createTileEntity(terrainEntity, tile, tileX, tileZ);

        events::terrain::TerrainTileAddedNotification notification;
        notification.terrainEntity = terrainEntity;
        notification.tileX = tileX;
        notification.tileZ = tileZ;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    bool TerrainService::streamOutTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ)
    {
        if (!terrainEntity.isValid())
            return false;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return false;

        auto& grid = *gridIt->second;
        terrain::TileCoord coord{tileX, tileZ};

        if (!grid.getTile(coord))
            return false;

        grid.removeTile(coord);

        // Note: saveDirty is intentionally NOT set here. Streaming out a tile
        // does not alter the saved file — the tile data remains on disk.

        // Find and destroy the child tile entity
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity parentEnt = internal::fromHandle(terrainEntity);
        if (registry.valid(parentEnt) && registry.all_of<components::ChildrenComponent>(parentEnt))
        {
            const auto& children = registry.get<components::ChildrenComponent>(parentEnt).children;
            for (auto childEnt : children)
            {
                if (!registry.valid(childEnt) ||
                    !registry.all_of<components::TerrainTileComponent>(childEnt))
                    continue;

                const auto& tc = registry.get<components::TerrainTileComponent>(childEnt);
                if (tc.tileX == tileX && tc.tileZ == tileZ)
                {
                    scene::Entity tileEntity(childEnt);
                    sceneGraph->removeEntity(tileEntity);
                    break;
                }
            }
        }

        events::terrain::TerrainTileRemovedNotification notification;
        notification.terrainEntity = terrainEntity;
        notification.tileX = tileX;
        notification.tileZ = tileZ;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    void TerrainService::commitStreamingChanges(EntityHandle terrainEntity)
    {
        if (!terrainEntity.isValid())
            return;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(terrainEntity);
        if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
        {
            auto& comp = registry.get<components::TerrainComponent>(ent);
            int32_t minX, minZ, maxX, maxZ;
            gridIt->second->computeBounds(minX, minZ, maxX, maxZ);
            comp.gridMinX = minX;
            comp.gridMinZ = minZ;
            comp.gridMaxX = maxX;
            comp.gridMaxZ = maxZ;
            comp.activeTileCount = static_cast<uint32_t>(gridIt->second->getTileCount());
        }
    }
}
