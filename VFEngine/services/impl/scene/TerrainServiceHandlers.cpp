#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
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

        dispatcher.registerQueryHandler<events::terrain::GetTerrainBakeGeometryQuery>(
            [this](const events::terrain::GetTerrainBakeGeometryQuery&)
            {
                return getTerrainBakeGeometry();
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainHeightfieldQuery>(
            [this](const events::terrain::GetTerrainHeightfieldQuery&)
            {
                return getTerrainHeightfield();
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
}
