#include "print/Log.hpp"
#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include <asset/AssetRef.hpp>
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/PaintBrushEvents.hpp"
#include "../../events/terrain/HoleBrushEvents.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"

namespace services
{
    void TerrainService::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        registerTerrainCoreHandlers(dispatcher);
        registerBrushHandlers(dispatcher);
        registerVegetationBrushHandlers(dispatcher);
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

        dispatcher.registerQueryHandler<events::terrain::GetTerrainHeightAtQuery>(
            [this](const events::terrain::GetTerrainHeightAtQuery& q)
            {
                return getTerrainHeightAt(q.worldX, q.worldZ);
            });

        dispatcher.registerCommandHandler<events::terrain::AddTerrainTileCommand>(
            [this](const events::terrain::AddTerrainTileCommand& cmd)
            {
                return addTile(cmd.terrainEntity, cmd.tileX, cmd.tileZ);
            });

        dispatcher.registerCommandHandler<events::terrain::RemoveTerrainTileCommand>(
            [this](const events::terrain::RemoveTerrainTileCommand& cmd)
            {
                return removeTile(cmd.terrainEntity, cmd.tileX, cmd.tileZ);
            });

        dispatcher.registerCommandHandler<events::terrain::SetTerrainStreamingEnabledCommand>(
            [this](const events::terrain::SetTerrainStreamingEnabledCommand& cmd)
            {
                auto it = worldStreamers.find(cmd.terrainEntity.id);
                if (it != worldStreamers.end() && it->second)
                    it->second->setEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::terrain::SetTerrainStreamingConfigCommand>(
            [this](const events::terrain::SetTerrainStreamingConfigCommand& cmd)
            {
                auto it = worldStreamers.find(cmd.terrainEntity.id);
                if (it != worldStreamers.end() && it->second)
                {
                    terrain::StreamingConfig config;
                    config.loadRadius = cmd.loadRadius;
                    config.unloadRadius = cmd.unloadRadius;
                    config.maxLoadsPerFrame = cmd.maxLoadsPerFrame;
                    config.maxUnloadsPerFrame = cmd.maxUnloadsPerFrame;
                    it->second->setConfig(config);
                }
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainStreamingConfigQuery>(
            [this](const events::terrain::GetTerrainStreamingConfigQuery& query)
            {
                auto it = worldStreamers.find(query.terrainEntity.id);
                if (it != worldStreamers.end() && it->second)
                {
                    const auto& cfg = it->second->getConfig();
                    events::terrain::StreamingConfigData data;
                    data.loadRadius = cfg.loadRadius;
                    data.unloadRadius = cfg.unloadRadius;
                    data.maxLoadsPerFrame = cfg.maxLoadsPerFrame;
                    data.maxUnloadsPerFrame = cfg.maxUnloadsPerFrame;
                    return data;
                }
                return events::terrain::StreamingConfigData{};
            });

        dispatcher.registerQueryHandler<events::terrain::IsTerrainStreamingEnabledQuery>(
            [this](const events::terrain::IsTerrainStreamingEnabledQuery& query)
            {
                auto it = worldStreamers.find(query.terrainEntity.id);
                if (it != worldStreamers.end() && it->second)
                    return it->second->isEnabled();
                return false;
            });

        dispatcher.registerCommandHandler<events::terrain::LoadAllTilesCommand>(
            [this](const events::terrain::LoadAllTilesCommand& cmd)
            {
                loadAllTiles(cmd.terrainEntity);
            });

        dispatcher.registerCommandHandler<events::terrain::BakeTerrainSVTCommand>(
            [this](const events::terrain::BakeTerrainSVTCommand& cmd)
            {
                return bakeTerrainSVT(cmd.terrainEntity);
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

        dispatcher.registerCommandHandler<events::holeBrush::ApplyHoleBrushCommand>(
            [this](const events::holeBrush::ApplyHoleBrushCommand& cmd)
            {
                applyHoleBrush(cmd.worldPosition, cmd.erase);
            });
    }

    void TerrainService::registerVegetationBrushHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::vegetationBrush::ApplyVegetationDensityBrushCommand>(
            [this](const events::vegetationBrush::ApplyVegetationDensityBrushCommand& cmd)
            {
                applyVegetationDensityBrush(cmd.worldPosition, cmd.deltaTime, cmd.invert, cmd.isFirstApplication);
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
                    auto& comp = registry.get<components::TerrainComponent>(ent);
                    auto& lifecycle = resource::AssetLifecycleManager::instance();

                    // Release old terrain material
                    auto newRef = asset::AssetRef::fromPath(cmd.materialPath);
                    if (comp.terrainMaterialRef.isValid() && comp.terrainMaterialRef != newRef)
                    {
                        lifecycle.release(comp.terrainMaterialRef.getGUID());
                    }

                    comp.terrainMaterialRef = newRef;

                    // Acquire new terrain material
                    if (newRef.isValid())
                    {
                        lifecycle.acquire(newRef.getGUID(), resource::AssetType::Material);
                    }

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

        dispatcher.registerCommandHandler<events::terrain::PrepareTerrainSaveCommand>(
            [this](const events::terrain::PrepareTerrainSaveCommand& cmd)
            {
                if (cmd.incremental)
                    return prepareSaveIncremental(cmd.terrainEntity.id);
                return prepareSave(cmd.terrainEntity.id);
            });

        dispatcher.registerCommandHandler<events::terrain::SaveTerrainCommand>(
            [this](const events::terrain::SaveTerrainCommand& cmd)
            {
                if (cmd.incremental)
                    return saveTerrainIncremental(cmd.terrainEntity.id, cmd.path);
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
        dispatcher.registerCommandHandler<events::terrain::BeginCreateTerrainCommand>(
            [this](const events::terrain::BeginCreateTerrainCommand& cmd) -> bool
            {
                return beginCreateTerrainAsync(cmd.config);
            });

        dispatcher.registerCommandHandler<events::terrain::PollCreateTerrainCommand>(
            [this](const events::terrain::PollCreateTerrainCommand&) -> TerrainCreationPollResult
            {
                return pollCreateTerrain();
            });

        dispatcher.registerCommandHandler<events::terrain::BeginTerrainLoadCommand>(
            [this](const events::terrain::BeginTerrainLoadCommand& cmd) -> bool
            {
                saveInProgress.store(true, std::memory_order_release);

                std::vector<uint64_t> toDelete;
                for (auto& [id, grid] : terrainGrids)
                    toDelete.push_back(id);
                for (auto id : toDelete)
                    deleteTerrain(EntityHandle{id});

                terrain::TerrainFileHeader header;
                std::vector<terrain::TileIndexEntry> index;
                uint64_t indexTableOffset = 0;

                if (!terrain::TerrainSerializer::readHeader(cmd.path, header, index, &indexTableOffset))
                {
                    vfLogError("TerrainService: Failed to read terrain header from {}", cmd.path);
                    saveInProgress.store(false, std::memory_order_release);
                    return false;
                }

                EntityHandle result = finishLoadTerrain(header, index, cmd.path, indexTableOffset);
                saveInProgress.store(false, std::memory_order_release);

                if (result.id != 0)
                {
                    events::terrain::TerrainLoadedNotification notification;
                    notification.terrainEntity = result;
                    notification.path = cmd.path;
                    events::EventDispatcher::instance().publish(notification);
                }

                vfLogInfo("TerrainService: Loaded terrain from {}", cmd.path);
                return result.id != 0;
            });

        dispatcher.registerCommandHandler<events::terrain::PollTerrainLoadCommand>(
            [this](const events::terrain::PollTerrainLoadCommand&) -> std::optional<EntityHandle>
            {
                return std::nullopt;
            });
    }
}
