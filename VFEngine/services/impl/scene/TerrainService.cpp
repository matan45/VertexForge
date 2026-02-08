#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/HeightmapLoader.hpp"
#include "terrain/BrushSampler.hpp"
#include "terrain/TerrainWeightMapAsset.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "terrain/WeightBrushApplicator.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "resource/ResourceManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/TerrainEvents.hpp"
#include "../../events/BrushEvents.hpp"
#include "../../events/PaintBrushEvents.hpp"
#include "../../events/PaintModeEvents.hpp"
#include "../../events/SculptModeEvents.hpp"
#include "../../events/SceneEvents.hpp"
#include "print/EditorLogger.hpp"

namespace services
{
    TerrainService::TerrainService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
    {
    }

    TerrainService::~TerrainService()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<events::terrain::CreateTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::DeleteTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::RemapTerrainEntitiesCommand>();
        dispatcher.unregisterCommandHandler<events::brush::ApplyBrushCommand>();
        dispatcher.unregisterCommandHandler<events::paintBrush::ApplyPaintBrushCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SetTerrainMaterialPathCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SaveWeightMapsCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::LoadWeightMapsCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SaveTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::LoadTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SetTerrainSaveLockCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::BeginTerrainLoadCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::PollTerrainLoadCommand>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainDataQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::HasTerrainComponentQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::HasTerrainTileComponentQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainTileDataQuery>();

        if (entityDeletedSubscription && entityDeletedSubscription->isValid())
        {
            dispatcher.unsubscribe(*entityDeletedSubscription);
        }

        if (sceneClearedSubscription && sceneClearedSubscription->isValid())
        {
            dispatcher.unsubscribe(*sceneClearedSubscription);
        }

        terrainGrids.clear();
    }

    void TerrainService::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::terrain::CreateTerrainCommand>(
            [this](const events::terrain::CreateTerrainCommand& cmd)
            {
                return createTerrain(cmd.config);
            });

        dispatcher.registerCommandHandler<events::terrain::DeleteTerrainCommand>(
            [this](const events::terrain::DeleteTerrainCommand& cmd)
            {
                return deleteTerrain(cmd.terrainEntity);
            });

        dispatcher.registerCommandHandler<events::terrain::RemapTerrainEntitiesCommand>(
            [this](const events::terrain::RemapTerrainEntitiesCommand&)
            {
                remapTerrainEntities();
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainDataQuery>(
            [this](const events::terrain::GetTerrainDataQuery& query)
            {
                return getTerrainData(query.entity);
            });

        dispatcher.registerQueryHandler<events::terrain::HasTerrainComponentQuery>(
            [this](const events::terrain::HasTerrainComponentQuery& query)
            {
                return hasTerrainComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::terrain::HasTerrainTileComponentQuery>(
            [this](const events::terrain::HasTerrainTileComponentQuery& query)
            {
                return hasTerrainTileComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainTileDataQuery>(
            [this](const events::terrain::GetTerrainTileDataQuery& query)
            {
                return getTerrainTileData(query.entity);
            });

        dispatcher.registerCommandHandler<events::brush::ApplyBrushCommand>(
            [this](const events::brush::ApplyBrushCommand& cmd)
            {
                applyBrush(cmd.worldPosition, cmd.deltaTime, cmd.invert, cmd.isFirstApplication);
            });

        dispatcher.registerCommandHandler<events::paintBrush::ApplyPaintBrushCommand>(
            [this](const events::paintBrush::ApplyPaintBrushCommand& cmd)
            {
                applyPaintBrush(cmd.worldPosition, cmd.deltaTime, cmd.invert, cmd.isFirstApplication);
            });

        dispatcher.registerCommandHandler<events::terrain::SetTerrainMaterialPathCommand>(
            [this](const events::terrain::SetTerrainMaterialPathCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity ent = internal::fromHandle(cmd.terrainEntity);
                if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
                {
                    registry.get<components::TerrainComponent>(ent).terrainMaterialPath = cmd.materialPath;

                    syncWeightMapLayerCount(cmd.terrainEntity.id, cmd.materialPath);
                }
            });

        dispatcher.registerCommandHandler<events::terrain::SaveWeightMapsCommand>(
            [this](const events::terrain::SaveWeightMapsCommand& cmd)
            {
                return saveWeightMaps(cmd.terrainEntity.id, cmd.path);
            });

        dispatcher.registerCommandHandler<events::terrain::LoadWeightMapsCommand>(
            [this](const events::terrain::LoadWeightMapsCommand& cmd)
            {
                return loadWeightMaps(cmd.terrainEntity.id, cmd.path);
            });

        dispatcher.registerCommandHandler<events::terrain::SaveTerrainCommand>(
            [this](const events::terrain::SaveTerrainCommand& cmd)
            {
                return saveTerrain(cmd.terrainEntity.id, cmd.path);
            });

        dispatcher.registerCommandHandler<events::terrain::LoadTerrainCommand>(
            [this](const events::terrain::LoadTerrainCommand& cmd)
            {
                return loadTerrain(cmd.path);
            });

        dispatcher.registerCommandHandler<events::terrain::SetTerrainSaveLockCommand>(
            [this](const events::terrain::SetTerrainSaveLockCommand& cmd)
            {
                saveInProgress.store(cmd.locked, std::memory_order_release);
            });

        dispatcher.registerCommandHandler<events::terrain::BeginTerrainLoadCommand>(
            [this](const events::terrain::BeginTerrainLoadCommand& cmd) -> bool
            {
                if (pendingLoad)
                {
                    vfLogWarning("TerrainService: Load already in progress");
                    return false;
                }

                // Block brush input during load
                saveInProgress.store(true, std::memory_order_release);

                // Delete all existing terrains
                std::vector<uint64_t> toDelete;
                for (auto& [id, grid] : terrainGrids)
                    toDelete.push_back(id);
                for (auto id : toDelete)
                    deleteTerrain(EntityHandle{id});

                pendingLoad = std::make_unique<PendingTerrainLoad>();
                pendingLoad->path = cmd.path;

                auto* pending = pendingLoad.get();
                pendingLoad->ioFuture = std::async(std::launch::async,
                    [pending]()
                    {
                        return terrain::TerrainSerializer::readHeader(
                            pending->path, pending->header, pending->index);
                    });

                events::terrain::TerrainLoadStartedNotification notification;
                notification.path = cmd.path;
                events::EventDispatcher::instance().publish(notification);

                vfLogInfo("TerrainService: Started async terrain load from {}", cmd.path);
                return true;
            });

        dispatcher.registerCommandHandler<events::terrain::PollTerrainLoadCommand>(
            [this](const events::terrain::PollTerrainLoadCommand&) -> std::optional<EntityHandle>
            {
                if (!pendingLoad)
                    return std::nullopt;

                if (pendingLoad->ioFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                    return std::nullopt;

                bool success = pendingLoad->ioFuture.get();

                if (!success)
                {
                    vfLogError("TerrainService: Async terrain load failed for {}", pendingLoad->path);
                    pendingLoad.reset();
                    saveInProgress.store(false, std::memory_order_release);
                    return EntityHandle{};
                }

                EntityHandle result = finishLoadTerrain(
                    pendingLoad->header, pendingLoad->index, pendingLoad->path);

                std::string loadPath = pendingLoad->path;
                pendingLoad.reset();
                saveInProgress.store(false, std::memory_order_release);

                if (result.id != 0)
                {
                    events::terrain::TerrainLoadedNotification notification;
                    notification.terrainEntity = result;
                    notification.path = loadPath;
                    events::EventDispatcher::instance().publish(notification);
                }

                return result;
            });

        auto token = dispatcher.subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& notification)
            {
                onEntityDeleted(notification.entity);
            });
        entityDeletedSubscription = std::make_unique<events::SubscriptionToken>(token);

        auto sceneToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                onSceneCleared();
            });
        sceneClearedSubscription = std::make_unique<events::SubscriptionToken>(sceneToken);
    }

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
        scene::Entity parentEntity(internal::fromHandle(parentHandle));

        for (auto* tile : grid.getAllTiles())
        {
            std::string tileName = "Tile_" + std::to_string(tile->coord.x) + "_" + std::to_string(tile->coord.z);
            scene::Entity tileEntity(tileName);
            parentEntity.addChildren(tileEntity);

            auto& tileComp = tileEntity.addComponent<components::TerrainTileComponent>();
            tileComp.tileX = tile->coord.x;
            tileComp.tileZ = tile->coord.z;
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

        terrainGrids.erase(terrainEntity.id);

        scene::Entity terrainEnt(entity);
        sceneGraph->removeEntity(terrainEnt);

        events::terrain::TerrainDeletedNotification notification;
        notification.terrainEntity = terrainEntity;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    std::optional<TerrainData> TerrainService::getTerrainData(EntityHandle entity) const
    {
        if (!entity.isValid())
            return std::nullopt;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return std::nullopt;

        if (!registry.all_of<components::TerrainComponent>(ent))
            return std::nullopt;

        const auto& comp = registry.get<components::TerrainComponent>(ent);

        TerrainData data;
        data.resolution = comp.resolution;
        data.worldTileSize = comp.worldTileSize;
        data.maxHeight = comp.maxHeight;
        data.minHeight = comp.minHeight;
        data.gridMinX = comp.gridMinX;
        data.gridMinZ = comp.gridMinZ;
        data.gridMaxX = comp.gridMaxX;
        data.gridMaxZ = comp.gridMaxZ;
        data.heightmapPath = comp.heightmapPath;
        data.terrainMaterialPath = comp.terrainMaterialPath;
        data.weightMapPath = comp.weightMapPath;
        data.tileCount = static_cast<uint32_t>((comp.gridMaxX - comp.gridMinX + 1) *
                                                (comp.gridMaxZ - comp.gridMinZ + 1));
        data.isActive = comp.isActive;
        data.isDirty = comp.isDirty;
        data.activeTileCount = comp.activeTileCount;
        data.visibleTileCount = comp.visibleTileCount;
        data.savePath = comp.savePath;
        data.saveDirty = comp.saveDirty;

        return data;
    }

    bool TerrainService::hasTerrainComponent(EntityHandle entity) const
    {
        if (!entity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return false;

        return registry.all_of<components::TerrainComponent>(ent);
    }

    std::string TerrainService::getTerrainMaterialPath() const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::TerrainComponent>();
        for (auto entity : view)
        {
            const auto& comp = view.get<components::TerrainComponent>(entity);
            if (!comp.terrainMaterialPath.empty())
            {
                return comp.terrainMaterialPath;
            }
        }
        return {};
    }

    bool TerrainService::hasTerrainTileComponent(EntityHandle entity) const
    {
        if (!entity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return false;

        return registry.all_of<components::TerrainTileComponent>(ent);
    }

    std::optional<TerrainTileData> TerrainService::getTerrainTileData(EntityHandle entity) const
    {
        if (!entity.isValid())
            return std::nullopt;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return std::nullopt;

        if (!registry.all_of<components::TerrainTileComponent>(ent))
            return std::nullopt;

        const auto& comp = registry.get<components::TerrainTileComponent>(ent);

        TerrainTileData data;
        data.tileX = comp.tileX;
        data.tileZ = comp.tileZ;
        data.currentLOD = comp.currentLOD;
        data.isVisible = comp.isVisible;
        data.isDirty = comp.isDirty;
        data.isGPUResident = comp.isGPUResident;
        data.boundingMinY = comp.boundingMinY;
        data.boundingMaxY = comp.boundingMaxY;

        return data;
    }

    std::vector<terrain::TerrainTile*> TerrainService::getRawVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<terrain::TerrainTile*> result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            (void)grid->updateLODs(cameraPosition);

            grid->regenerateDirtyTiles(cameraPosition);

            auto visibleTiles = grid->getVisibleTiles(frustum);

            for (terrain::TerrainTile* tile : visibleTiles)
            {
                if (tile && tile->isVisible)
                {
                    result.push_back(tile);
                }
            }
        }

        return result;
    }

    void TerrainService::remapTerrainEntities()
    {
        if (terrainGrids.empty())
            return;

        std::vector<std::unique_ptr<terrain::TerrainGrid>> grids;
        std::vector<std::shared_ptr<terrain::TerrainFileCache>> caches;
        for (auto& [id, grid] : terrainGrids)
        {
            grids.push_back(std::move(grid));
            auto cacheIt = fileCaches.find(id);
            caches.push_back(cacheIt != fileCaches.end() ? std::move(cacheIt->second) : nullptr);
        }
        terrainGrids.clear();
        fileCaches.clear();

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
            gridIndex++;
        }
    }

    void TerrainService::onEntityDeleted(EntityHandle entity)
    {
        if (!entity.isValid())
            return;

        auto it = terrainGrids.find(entity.id);
        if (it != terrainGrids.end())
        {
            terrainGrids.erase(it);
            fileCaches.erase(entity.id);

            events::terrain::TerrainDeletedNotification notification;
            notification.terrainEntity = entity;
            events::EventDispatcher::instance().publish(notification);
        }
    }

    void TerrainService::onSceneCleared()
    {
        if (terrainGrids.empty())
            return;

        events::terrain::TerrainDeletedNotification notification;
        events::EventDispatcher::instance().publish(notification);

        terrainGrids.clear();
        fileCaches.clear();

        vfLogInfo("TerrainService: Cleared all terrains on scene clear");
    }

    void TerrainService::applyBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::sculpt::GetSculptTargetEntityQuery{});
        if (!targetEntity.has_value())
        {
            return;
        }

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
        {
            return;
        }

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

        // Get file cache for on-demand height loading
        auto cacheIt = fileCaches.find(targetEntity->id);
        auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        std::vector<terrain::TileCoord> modifiedTiles;
        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
            {
                continue;
            }

            if (!brushComputeProvider)
            {
                continue;
            }

            // Ensure height data is loaded from file for sculpting
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

        if (!modifiedTiles.empty())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(*targetEntity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).saveDirty = true;
            }
        }
    }

    void TerrainService::applyPaintBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::paint::GetPaintTargetEntityQuery{});
        if (!targetEntity.has_value())
        {
            return;
        }

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
        {
            return;
        }

        terrain::TerrainGrid* grid = gridIt->second.get();

        auto brushParams = dispatcher.query(events::paintBrush::GetPaintBrushParamsQuery{});
        auto brushType = dispatcher.query(events::paintBrush::GetPaintBrushTypeQuery{});

        float worldTileSize = 32.0f;
        const auto& allTiles = grid->getAllTiles();
        if (!allTiles.empty())
        {
            worldTileSize = allTiles[0]->config.worldTileSize;
        }

        // Build overlay bitmask from terrain material blend modes
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

        glm::vec2 brushCenter(worldPosition.x, worldPosition.z);
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            brushCenter, brushParams.radius, worldTileSize);

        // Get file cache for on-demand weight loading
        auto cacheIt = fileCaches.find(targetEntity->id);
        auto paintFileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
            {
                continue;
            }

            // Ensure height/weight data is loaded from file for painting
            if (paintFileCache && !tile->hasHeightData())
                paintFileCache->ensureHeightsLoaded(*tile);
            if (paintFileCache)
                paintFileCache->markDirty(coord);

            if (!tile->hasWeightMap())
            {
                continue;
            }

            if (brushParams.activeLayer >= terrain::MAX_TERRAIN_LAYERS)
            {
                continue;
            }

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

    bool TerrainService::saveWeightMaps(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        auto allTiles = gridIt->second->getAllTiles();
        if (allTiles.empty())
        {
            vfLogWarning("TerrainService: No tiles to save weight maps for");
            return true;
        }

        uint32_t resolution = allTiles[0]->config.getVertexCount();

        // Get material path from TerrainComponent
        std::string materialPath;
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});
        if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
        {
            materialPath = registry.get<components::TerrainComponent>(ent).terrainMaterialPath;
        }

        std::unordered_map<terrain::TileCoord, terrain::TileWeightMapData, terrain::TileCoordHash> tileWeights;
        for (const auto* tile : allTiles)
        {
            if (tile->hasWeightMap())
            {
                tileWeights.emplace(tile->coord, tile->weightMap);
            }
        }

        return terrain::TerrainWeightMapAsset::save(path, tileWeights, resolution, materialPath);
    }

    bool TerrainService::loadWeightMaps(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        std::string materialPath;
        auto loadedWeights = terrain::TerrainWeightMapAsset::load(path, &materialPath);
        if (loadedWeights.empty())
        {
            return false;
        }

        // Restore material path if present
        if (!materialPath.empty())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).terrainMaterialPath = materialPath;
                syncWeightMapLayerCount(terrainEntityId, materialPath);
            }
        }

        auto allTiles = gridIt->second->getAllTiles();
        uint32_t loadedCount = 0;

        for (auto* tile : allTiles)
        {
            auto it = loadedWeights.find(tile->coord);
            if (it != loadedWeights.end())
            {
                if (it->second.resolution == tile->config.getVertexCount())
                {
                    tile->weightMap = std::move(it->second);
                    tile->weightMapDirty = true;
                    tile->weightMapGPUDirty = true;
                    loadedCount++;
                }
                else
                {
                    vfLogWarning("TerrainService: Weight map resolution mismatch for tile ({}, {}): "
                                 "expected {}, got {}",
                                 tile->coord.x, tile->coord.z,
                                 tile->config.getVertexCount(), it->second.resolution);
                }
            }
        }

        vfLogInfo("TerrainService: Loaded weight maps for {} of {} tiles", loadedCount, allTiles.size());
        return true;
    }

    void TerrainService::syncWeightMapLayerCount(uint64_t terrainEntityId, const std::string& materialPath)
    {
        if (materialPath.empty())
        {
            return;
        }

        auto materialData = resource::ResourceManager::loadTerrainMaterial(materialPath);
        if (!materialData)
        {
            return;
        }

        uint8_t layerCount = materialData->activeLayerCount;
        if (layerCount == 0)
        {
            layerCount = 1;
        }

        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            return;
        }

        terrain::TerrainGrid* grid = gridIt->second.get();
        grid->updateWeightMapLayerCount(layerCount);
    }

    bool TerrainService::saveTerrain(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

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
        for (int i = 0; i < 4; ++i)
            tileConfig.lodDistances[i] = comp.lodDistances[i];

        // Ensure all tiles have data loaded from file before saving.
        // The serializer needs heightData + lodLevels for every tile.
        auto cacheIt = fileCaches.find(terrainEntityId);
        if (cacheIt != fileCaches.end() && cacheIt->second)
        {
            auto& grid = *gridIt->second;
            auto& generator = grid.getGenerator();
            auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
                return grid.getTile(coord);
            };

            for (auto* tile : grid.getAllTiles())
            {
                if (tile && !tile->hasHeightData())
                    cacheIt->second->ensureHeightsLoaded(*tile);
                if (tile && !tile->hasAnyLODData())
                    cacheIt->second->ensureLODsLoaded(*tile, generator, getTile);
            }
        }

        bool result = terrain::TerrainSerializer::save(
            path, *gridIt->second, tileConfig,
            comp.gridMinX, comp.gridMinZ, comp.gridMaxX, comp.gridMaxZ,
            comp.terrainMaterialPath);

        if (result)
        {
            auto& mutableComp = registry.get<components::TerrainComponent>(ent);
            mutableComp.savePath = path;
            mutableComp.saveDirty = false;

            // Refresh file cache index after save (offsets changed, dirty tiles now clean)
            auto cacheIt = fileCaches.find(terrainEntityId);
            if (cacheIt != fileCaches.end() && cacheIt->second)
            {
                cacheIt->second->refreshIndex(path);
            }
            else
            {
                // First save of a newly created terrain - create a file cache
                terrain::TerrainFileHeader newHeader;
                std::vector<terrain::TileIndexEntry> newIndex;
                if (terrain::TerrainSerializer::readHeader(path, newHeader, newIndex))
                {
                    auto cache = std::make_shared<terrain::TerrainFileCache>(path, newHeader, newIndex);
                    fileCaches[terrainEntityId] = cache;
                    gridIt->second->setFileCache(cache);
                }
            }

            events::terrain::TerrainSavedNotification savedNotification;
            savedNotification.terrainEntity = EntityHandle{terrainEntityId};
            savedNotification.path = path;
            events::EventDispatcher::instance().publish(savedNotification);

            vfLogInfo("TerrainService: Saved terrain to {}", path);
        }

        return result;
    }

    EntityHandle TerrainService::loadTerrain(const std::string& path)
    {
        terrain::TerrainFileHeader header;
        std::vector<terrain::TileIndexEntry> index;

        if (!terrain::TerrainSerializer::readHeader(path, header, index))
        {
            vfLogError("TerrainService: Failed to read terrain header from {}", path);
            return {};
        }

        return finishLoadTerrain(header, index, path);
    }

    EntityHandle TerrainService::finishLoadTerrain(
        terrain::TerrainFileHeader& header,
        std::vector<terrain::TileIndexEntry>& index,
        const std::string& path)
    {
        terrain::TerrainTileConfig tileConfig;
        tileConfig.resolution = static_cast<terrain::TileResolution>(header.resolution);
        tileConfig.worldTileSize = header.worldTileSize;
        tileConfig.maxHeight = header.maxHeight;
        tileConfig.minHeight = header.minHeight;
        tileConfig.lodDistances = header.lodDistances;
        tileConfig.skirtDepth = header.skirtDepth;

        auto grid = std::make_unique<terrain::TerrainGrid>(tileConfig);
        grid->loadMetadataOnly(header, index);

        // Create file cache for on-demand streaming
        auto cache = std::make_shared<terrain::TerrainFileCache>(path, header, index);
        grid->setFileCache(cache);

        scene::Entity parentEntity("Terrain");
        sceneGraph->addChild(sceneGraph->GetRoot(), parentEntity);

        auto& terrainComp = parentEntity.addComponent<components::TerrainComponent>();
        terrainComp.resolution = header.resolution;
        terrainComp.worldTileSize = header.worldTileSize;
        terrainComp.maxHeight = header.maxHeight;
        terrainComp.minHeight = header.minHeight;
        terrainComp.gridMinX = header.gridMinX;
        terrainComp.gridMinZ = header.gridMinZ;
        terrainComp.gridMaxX = header.gridMaxX;
        terrainComp.gridMaxZ = header.gridMaxZ;
        terrainComp.lodDistances = header.lodDistances;
        terrainComp.terrainMaterialPath = header.materialPath;
        terrainComp.isActive = true;
        terrainComp.isDirty = false;
        terrainComp.activeTileCount = header.tileCount;
        terrainComp.visibleTileCount = 0;
        terrainComp.savePath = path;
        terrainComp.saveDirty = false;

        EntityHandle parentHandle = internal::toHandle(parentEntity.getHandle());

        createTileEntities(parentHandle, *grid);

        terrainGrids[parentHandle.id] = std::move(grid);
        fileCaches[parentHandle.id] = cache;

        // Rebind material: sync weight map layer count with material layers
        if (!header.materialPath.empty())
        {
            syncWeightMapLayerCount(parentHandle.id, header.materialPath);
        }

        // Publish notification so GPU adapter picks up the new terrain
        events::terrain::TerrainCreatedNotification notification;
        notification.terrainEntity = parentHandle;
        notification.config.resolution = header.resolution;
        notification.config.worldTileSize = header.worldTileSize;
        notification.config.maxHeight = header.maxHeight;
        notification.config.minHeight = header.minHeight;
        notification.config.terrainMaterialPath = header.materialPath;
        notification.config.tilesX = header.gridMaxX - header.gridMinX + 1;
        notification.config.tilesZ = header.gridMaxZ - header.gridMinZ + 1;
        for (int i = 0; i < 4; ++i)
            notification.config.lodDistances[i] = header.lodDistances[i];
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("TerrainService: Loaded terrain with {} tiles from {}", header.tileCount, path);

        return parentHandle;
    }

    bool TerrainService::ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel)
    {
        // Find the grid that owns this tile
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
}
