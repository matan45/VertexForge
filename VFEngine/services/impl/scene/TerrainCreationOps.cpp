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
#include "threading/JobSystem.hpp"
#include <asset/AssetRef.hpp>
#include <algorithm>

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
            float terrainMinX = static_cast<float>(minX) * config.worldTileSize;
            float terrainMinZ = static_cast<float>(minZ) * config.worldTileSize;
            float terrainWidth = static_cast<float>(config.tilesX) * config.worldTileSize;
            float terrainDepth = static_cast<float>(config.tilesZ) * config.worldTileSize;

            // Check if SVT heightmap — use streaming sampler (no full image in RAM)
            bool isSVT = config.heightmapPath.size() > 6 &&
                         config.heightmapPath.substr(config.heightmapPath.size() - 6) == ".vfSVT";

            bool heightmapLoaded = false;

            if (isSVT)
            {
                terrain::TerrainBounds hBounds{terrainMinX, terrainMinZ,
                    terrainWidth, terrainDepth, config.minHeight, config.maxHeight};
                auto sampler = terrain::createStreamingHeightSamplerFromSVT(
                    config.heightmapPath, hBounds);
                if (sampler)
                {
                    grid->setHeightSampler(std::move(sampler));
                    vfLogInfo("Streaming SVT heightmap: {}", config.heightmapPath);
                    heightmapLoaded = true;
                }
            }

            if (!heightmapLoaded)
            {
                auto heightmapData = terrain::HeightmapLoader::load(config.heightmapPath);
                if (heightmapData && heightmapData->isValid())
                {
                    terrain::TerrainBounds hBounds{terrainMinX, terrainMinZ,
                        terrainWidth, terrainDepth, config.minHeight, config.maxHeight};
                    grid->setHeightSampler(terrain::createHeightSamplerFromMap(
                        heightmapData, hBounds));
                    vfLogInfo("Loaded heightmap from: {}", config.heightmapPath);
                }
                else
                {
                    vfLogWarning("Failed to load heightmap: {}, creating flat terrain", config.heightmapPath);
                    grid->setHeightSampler([](float, float) -> float { return 0.0f; });
                }
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
        terrainComp.heightmapPath = config.heightmapPath;
        terrainComp.terrainMaterialRef = asset::AssetRef::fromPath(config.terrainMaterialPath);
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
        if (comp.terrainMaterialRef.isValid())
            resource::ResourceManager::invalidateTerrainMaterialCache(comp.terrainMaterialRef);

        if (physicsProvider)
            physicsProvider->removeTerrainCollider(terrainEntity);

        pendingPhysicsTiles.erase(
            std::remove_if(pendingPhysicsTiles.begin(), pendingPhysicsTiles.end(),
                [&](const auto& p) { return p.first == terrainEntity.id; }),
            pendingPhysicsTiles.end());

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

    TileAsyncLoadResult TerrainService::asyncLoadTileLODData(int32_t coordX, int32_t coordZ)
    {
        TileAsyncLoadResult result;
        result.coordX = coordX;
        result.coordZ = coordZ;

        terrain::TileCoord coord{coordX, coordZ};

        for (auto& [entityId, grid] : terrainGrids)
        {
            if (!grid->getTile(coord))
                continue;

            auto cacheIt = fileCaches.find(entityId);
            if (cacheIt == fileCaches.end() || !cacheIt->second)
                return result;

            auto& cache = *cacheIt->second;
            const auto& indexMap = cache.getIndexMap();
            auto indexIt = indexMap.find(coord);
            if (indexIt == indexMap.end())
                return result;

            const auto& entry = indexIt->second;
            if (entry.heightDataOffset == 0)
                return result;

            if (cache.hasMeshletCache() && entry.meshletDataOffset != 0)
            {
                if (terrain::TerrainSerializer::readTileLODData(cache.getFilePath(), entry, result.lodData))
                {
                    result.success = true;

                    if (entry.weightDataOffset != 0)
                    {
                        if (terrain::TerrainSerializer::readTileWeights(cache.getFilePath(), entry, result.weightMap))
                            result.hasWeightMap = true;
                    }

                    if (entry.holeMaskDataOffset != 0)
                    {
                        if (terrain::TerrainSerializer::readTileHoleMask(cache.getFilePath(), entry, result.holeMask))
                            result.hasHoleMask = true;
                    }
                }
            }

            return result;
        }

        return result;
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
        tileComp.isVisible = tile->isVisible;
        tileComp.isDirty = tile->isDirty;
        tileComp.isGPUResident = false;
        tileComp.boundingMinY = tile->worldBounds.min.y;
        tileComp.boundingMaxY = tile->worldBounds.max.y;

        auto& transform = tileEntity.getComponent<components::TransformComponent>();
        transform.position = tile->worldOrigin;
        transform.isDirty = true;
    }

    bool TerrainService::beginCreateTerrainAsync(const TerrainCreationData& config)
    {
        if (pendingCreation)
        {
            vfLogWarning("TerrainService: Terrain creation already in progress");
            return false;
        }

        auto pending = std::make_shared<PendingTerrainCreation>();
        pending->config = config;
        pending->progress.store(0.0f);
        pending->done.store(false);

        auto progressPtr = pending;
        pending->future = threading::JobSystem::instance().submit(
            [config, progressPtr]() -> std::unique_ptr<terrain::TerrainGrid>
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

                int32_t halfX = config.tilesX / 2;
                int32_t halfZ = config.tilesZ / 2;
                int32_t minX = -halfX;
                int32_t minZ = -halfZ;
                int32_t maxX = config.tilesX - halfX - 1;
                int32_t maxZ = config.tilesZ - halfZ - 1;

                auto grid = std::make_unique<terrain::TerrainGrid>(tileConfig);

                progressPtr->progress.store(0.05f);

                // Load heightmap (I/O heavy)
                if (!config.heightmapPath.empty())
                {
                    float terrainMinX = static_cast<float>(minX) * config.worldTileSize;
                    float terrainMinZ = static_cast<float>(minZ) * config.worldTileSize;
                    float terrainWidth = static_cast<float>(config.tilesX) * config.worldTileSize;
                    float terrainDepth = static_cast<float>(config.tilesZ) * config.worldTileSize;

                    bool isSVT = config.heightmapPath.size() > 6 &&
                                 config.heightmapPath.substr(config.heightmapPath.size() - 6) == ".vfSVT";

                    bool heightmapLoaded = false;
                    if (isSVT)
                    {
                        terrain::TerrainBounds hBounds{terrainMinX, terrainMinZ,
                            terrainWidth, terrainDepth, config.minHeight, config.maxHeight};
                        auto sampler = terrain::createStreamingHeightSamplerFromSVT(
                            config.heightmapPath, hBounds);
                        if (sampler)
                        {
                            grid->setHeightSampler(std::move(sampler));
                            heightmapLoaded = true;
                        }
                    }

                    if (!heightmapLoaded)
                    {
                        auto heightmapData = terrain::HeightmapLoader::load(config.heightmapPath);
                        if (heightmapData && heightmapData->isValid())
                        {
                            terrain::TerrainBounds hBounds{terrainMinX, terrainMinZ,
                                terrainWidth, terrainDepth, config.minHeight, config.maxHeight};
                            grid->setHeightSampler(terrain::createHeightSamplerFromMap(
                                heightmapData, hBounds));
                        }
                        else
                        {
                            grid->setHeightSampler([](float, float) -> float { return 0.0f; });
                        }
                    }
                }
                else
                {
                    grid->setHeightSampler([](float, float) -> float { return 0.0f; });
                }

                progressPtr->progress.store(0.2f);

                // Generate tiles (already uses enkiTS internally)
                grid->createGrid(minX, minZ, maxX, maxZ,
                    [&progressPtr](float p, const std::string&)
                    {
                        progressPtr->progress.store(0.2f + p * 0.75f);
                    });

                progressPtr->progress.store(1.0f);
                progressPtr->done.store(true);
                return grid;
            },
            threading::JobPriority::NORMAL
        );

        pendingCreation = std::move(pending);

        events::terrain::TerrainCreationProgressNotification progressNotification;
        progressNotification.progress = 0.0f;
        progressNotification.stage = "Starting terrain creation...";
        events::EventDispatcher::instance().publish(progressNotification);

        return true;
    }

    TerrainCreationPollResult TerrainService::pollCreateTerrain()
    {
        TerrainCreationPollResult result;

        if (!pendingCreation)
        {
            result.inProgress = false;
            return result;
        }

        float progress = pendingCreation->progress.load();
        result.inProgress = true;
        result.progress = progress;
        result.stage = progress < 0.2f ? "Loading heightmap..." : "Generating tiles...";

        if (!pendingCreation->done.load())
            return result;

        // Grid is ready — finalize on main thread
        auto grid = pendingCreation->future.get();
        auto config = pendingCreation->config;
        pendingCreation.reset();

        int32_t halfX = config.tilesX / 2;
        int32_t halfZ = config.tilesZ / 2;
        int32_t minX = -halfX;
        int32_t minZ = -halfZ;
        int32_t maxX = config.tilesX - halfX - 1;
        int32_t maxZ = config.tilesZ - halfZ - 1;

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
        terrainComp.heightmapPath = config.heightmapPath;
        terrainComp.terrainMaterialRef = asset::AssetRef::fromPath(config.terrainMaterialPath);
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
            loadWeightMaps(parentHandle.id, config.weightMapPath);

        if (!config.terrainMaterialPath.empty())
            syncWeightMapLayerCount(parentHandle.id, config.terrainMaterialPath);

        events::terrain::TerrainCreationProgressNotification progressDone;
        progressDone.progress = 1.0f;
        progressDone.stage = "Complete";
        events::EventDispatcher::instance().publish(progressDone);

        events::terrain::TerrainCreatedNotification notification;
        notification.terrainEntity = parentHandle;
        notification.config = config;
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("Created terrain with {} tiles (async)", config.tilesX * config.tilesZ);

        result.inProgress = false;
        result.progress = 1.0f;
        result.stage = "Complete";
        result.result = parentHandle;
        return result;
    }
}
