#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "resource/ResourceManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/TerrainEvents.hpp"
#include "../../events/BrushEvents.hpp"
#include "../../events/PaintBrushEvents.hpp"
#include "../../events/SceneEvents.hpp"
#include "../../events/PhysicsEvents.hpp"
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
        dispatcher.unregisterCommandHandler<events::terrain::SetTerrainColliderPropertiesCommand>();
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

        registerTerrainCoreHandlers(dispatcher);
        registerBrushHandlers(dispatcher);
        registerTerrainDataHandlers(dispatcher);
        registerAsyncLoadHandlers(dispatcher);

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

    void TerrainService::registerTerrainCoreHandlers(::events::EventDispatcher& dispatcher)
    {
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

        dispatcher.registerQueryHandler<events::terrain::GetTerrainGeometryQuery>(
            [this](const events::terrain::GetTerrainGeometryQuery&)
            {
                return getTerrainGeometryForNavmesh();
            });
    }

    void TerrainService::registerBrushHandlers(::events::EventDispatcher& dispatcher)
    {
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
    }

    void TerrainService::registerTerrainDataHandlers(::events::EventDispatcher& dispatcher)
    {
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

        dispatcher.registerCommandHandler<events::physics::AddTerrainColliderCommand>(
            [this](const events::physics::AddTerrainColliderCommand& cmd)
            {
                return addTerrainCollider(cmd.terrainEntity);
            });

        dispatcher.registerCommandHandler<events::physics::RemoveTerrainColliderCommand>(
            [this](const events::physics::RemoveTerrainColliderCommand& cmd)
            {
                removeTerrainCollider(cmd.terrainEntity);
            });

        dispatcher.registerQueryHandler<events::physics::HasTerrainColliderQuery>(
            [this](const events::physics::HasTerrainColliderQuery& query)
            {
                return hasTerrainCollider(query.terrainEntity);
            });

        dispatcher.registerCommandHandler<events::terrain::SetTerrainColliderPropertiesCommand>(
            [this](const events::terrain::SetTerrainColliderPropertiesCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity ent = internal::fromHandle(cmd.entity);
                if (registry.valid(ent) && registry.all_of<components::TerrainColliderComponent>(ent))
                {
                    auto& cc = registry.get<components::TerrainColliderComponent>(ent);
                    cc.collisionLayer = cmd.collisionLayer;
                    cc.friction = cmd.friction;
                    cc.restitution = cmd.restitution;
                }
            });
    }

    void TerrainService::registerAsyncLoadHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::terrain::BeginTerrainLoadCommand>(
            [this](const events::terrain::BeginTerrainLoadCommand& cmd) -> bool
            {
                if (pendingLoad)
                {
                    vfLogWarning("TerrainService: Load already in progress");
                    return false;
                }

                saveInProgress.store(true, std::memory_order_release);

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

        if (registry.all_of<components::TerrainColliderComponent>(ent))
        {
            const auto& cc = registry.get<components::TerrainColliderComponent>(ent);
            data.colliderCollisionLayer = cc.collisionLayer;
            data.colliderFriction = cc.friction;
            data.colliderRestitution = cc.restitution;
        }

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

    void TerrainService::onEntityDeleted(EntityHandle entity)
    {
        if (!entity.isValid())
            return;

        auto it = terrainGrids.find(entity.id);
        if (it != terrainGrids.end())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(entity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                const auto& comp = registry.get<components::TerrainComponent>(ent);
                if (!comp.terrainMaterialPath.empty())
                    resource::ResourceManager::invalidateTerrainMaterialCache(comp.terrainMaterialPath);
            }

            if (physicsProvider)
                physicsProvider->removeTerrainCollider(entity);

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

        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto& [id, _] : terrainGrids)
        {
            if (physicsProvider)
                physicsProvider->removeTerrainCollider(EntityHandle{id});

            entt::entity ent = internal::fromHandle(EntityHandle{id});
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                const auto& comp = registry.get<components::TerrainComponent>(ent);
                if (!comp.terrainMaterialPath.empty())
                    resource::ResourceManager::invalidateTerrainMaterialCache(comp.terrainMaterialPath);
            }
        }

        events::terrain::TerrainDeletedNotification notification;
        events::EventDispatcher::instance().publish(notification);

        terrainGrids.clear();
        fileCaches.clear();

        vfLogInfo("TerrainService: Cleared all terrains on scene clear");
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

    events::terrain::TerrainGeometryResult TerrainService::getTerrainGeometryForNavmesh()
    {
        events::terrain::TerrainGeometryResult result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            auto& generator = grid->getGenerator();
            auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
                return grid->getTile(coord);
            };

            auto allTiles = grid->getAllTiles();
            for (auto* tile : allTiles)
            {
                if (!tile || !tile->hasHeightData())
                    continue;

                if (!tile->hasLODData(0))
                {
                    generator.regenerateLOD(*tile, 0, getTile);
                }

                const auto& lod0 = tile->lodLevels[0];
                int baseVertex = static_cast<int>(result.vertices.size() / 3);

                const glm::vec3& origin = tile->worldOrigin;
                for (const auto& vertex : lod0.vertices)
                {
                    glm::vec3 worldPos = origin + vertex.position;
                    result.vertices.push_back(worldPos.x);
                    result.vertices.push_back(worldPos.y);
                    result.vertices.push_back(worldPos.z);

                    if (result.vertices.size() == 3)
                    {
                        result.boundsMin = worldPos;
                        result.boundsMax = worldPos;
                    }
                    else
                    {
                        result.boundsMin = glm::min(result.boundsMin, worldPos);
                        result.boundsMax = glm::max(result.boundsMax, worldPos);
                    }
                }

                for (size_t i = 0; i < lod0.indices.size(); i += 3)
                {
                    result.triangles.push_back(baseVertex + static_cast<int>(lod0.indices[i]));
                    result.triangles.push_back(baseVertex + static_cast<int>(lod0.indices[i + 1]));
                    result.triangles.push_back(baseVertex + static_cast<int>(lod0.indices[i + 2]));
                }
            }
        }

        return result;
    }
}
