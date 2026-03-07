#include "WorldSectorServiceImpl.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "world/WorldSectorSerialization.hpp"
#include "world/WorldDefinitionSerialization.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "print/Log.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>

namespace
{
    bool isManagedBySeparateSystem(const scene::Entity& entity)
    {
        return entity.hasComponent<components::TerrainComponent>()
            || entity.hasComponent<components::TerrainTileComponent>()
            || entity.hasComponent<components::WaterComponent>()
            || entity.hasComponent<components::WaterTileComponent>()
            || entity.hasComponent<components::IBLComponent>()
            || entity.hasComponent<components::CameraComponent>();
    }
}

namespace services
{
    WorldSectorServiceImpl::WorldSectorServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph))
    {
        entityLoader.setOnEntityLoaded([this](uint64_t uuid, const world::SectorCoord& coord)
        {
            sectorManager.assignEntityToSector(uuid,
                glm::vec3(
                    (static_cast<float>(coord.x) + 0.5f) * sectorManager.getConfig().sectorWorldSize,
                    0.0f,
                    (static_cast<float>(coord.z) + 0.5f) * sectorManager.getConfig().sectorWorldSize
                ));
        });

        entityLoader.setOnEntityUnloaded([this](uint64_t uuid, const world::SectorCoord& coord)
        {
            sectorManager.removeEntityFromSector(uuid, coord);
        });

        entityLoader.setOnEntityPostLoad([](uint64_t uuid, const std::string& meshPath, const std::string& animatorPath)
        {
            resource::AssetLifecycleManager::instance().acquire(meshPath, resource::AssetType::Mesh);

            // Find the entity by UUID and publish mesh notification
            auto& registry = scene::EntityRegistry::getRegistry();
            auto uuidView = registry.view<components::UUIDComponent>();
            for (auto entity : uuidView)
            {
                if (uuidView.get<components::UUIDComponent>(entity).id.getValue() == uuid)
                {
                    ::events::scene::MeshDataChangedNotification meshNotif;
                    meshNotif.entity = internal::toHandle(entity);
                    meshNotif.meshPath = meshPath;
                    meshNotif.animatorPath = animatorPath;
                    ::events::EventDispatcher::instance().publish(meshNotif);
                    break;
                }
            }
        });

        entityLoader.setOnEntityPreUnload([](uint64_t, const std::string& meshPath)
        {
            if (!meshPath.empty())
            {
                resource::AssetLifecycleManager::instance().release(meshPath);
            }
        });
    }

    void WorldSectorServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<::events::world::CreateWorldCommand>(
            [this](const ::events::world::CreateWorldCommand& cmd)
            {
                return createWorld(cmd.name, cmd.filePath, cmd.sectorConfig, cmd.streamingConfig);
            });

        dispatcher.registerCommandHandler<::events::world::SaveWorldCommand>(
            [this](const ::events::world::SaveWorldCommand& cmd)
            {
                return saveWorld(cmd.filePath);
            });

        dispatcher.registerCommandHandler<::events::world::LoadWorldCommand>(
            [this](const ::events::world::LoadWorldCommand& cmd)
            {
                return loadWorld(cmd.filePath);
            });

        dispatcher.registerCommandHandler<::events::world::SaveSectorCommand>(
            [this](const ::events::world::SaveSectorCommand& cmd)
            {
                return saveSector(cmd.coord, cmd.filePath);
            });

        dispatcher.registerCommandHandler<::events::world::LoadSectorCommand>(
            [this](const ::events::world::LoadSectorCommand& cmd)
            {
                return loadSector(cmd.coord);
            });

        dispatcher.registerCommandHandler<::events::world::UnloadSectorCommand>(
            [this](const ::events::world::UnloadSectorCommand& cmd)
            {
                return unloadSector(cmd.coord);
            });

        dispatcher.registerCommandHandler<::events::world::UpdateWorldStreamingCommand>(
            [this](const ::events::world::UpdateWorldStreamingCommand&)
            {
                update();
            });

        dispatcher.registerQueryHandler<::events::world::GetSectorAtPositionQuery>(
            [this](const ::events::world::GetSectorAtPositionQuery& q)
                -> std::optional<world::SectorCoord>
            {
                if (!worldMode) return std::nullopt;
                return sectorManager.worldPositionToSectorCoord(q.position);
            });

        dispatcher.registerQueryHandler<::events::world::GetSectorStateQuery>(
            [this](const ::events::world::GetSectorStateQuery& q) -> world::SectorState
            {
                const auto* sector = sectorManager.getSector(q.coord);
                if (!sector) return world::SectorState::Unloaded;
                return sector->state;
            });

        dispatcher.registerQueryHandler<::events::world::IsWorldModeQuery>(
            [this](const ::events::world::IsWorldModeQuery&)
            {
                return worldMode;
            });

        dispatcher.registerQueryHandler<::events::world::GetWorldStreamingStatsQuery>(
            [this](const ::events::world::GetWorldStreamingStatsQuery&)
            {
                return streamer.getConfig();
            });

        // Subscribe to transform changes for cross-sector entity migration
        transformChangedToken = dispatcher.subscribe<::events::scene::TransformChangedNotification>(
            [this](const ::events::scene::TransformChangedNotification& notif)
            {
                if (!worldMode) return;

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(notif.entity);
                if (!registry.valid(entity)) return;

                scene::Entity sceneEntity(entity);
                if (isManagedBySeparateSystem(sceneEntity)) return;

                uint64_t uuid = sceneEntity.getUUID().getValue();
                if (sectorManager.hasEntitySector(uuid))
                {
                    onTransformChanged(uuid, notif.newTransform.position);
                }
            });
    }

    void WorldSectorServiceImpl::update()
    {
        if (!worldMode)
            return;

        // Read camera position from the ECS registry
        glm::vec3 cameraPos(0.0f);
        auto& registry = scene::EntityRegistry::getRegistry();

        // Try WorldTransformComponent first (valid after scene graph update in runtime)
        auto worldCamView = registry.view<components::CameraComponent, components::WorldTransformComponent>();
        bool foundCamera = false;
        for (auto entity : worldCamView)
        {
            auto& camComp = worldCamView.get<components::CameraComponent>(entity);
            if (camComp.isPrimary)
            {
                auto& wt = worldCamView.get<components::WorldTransformComponent>(entity);
                cameraPos = glm::vec3(wt.worldMatrix[3]);
                foundCamera = true;
                break;
            }
        }

        // Fall back to local TransformComponent (editor edit mode)
        if (!foundCamera)
        {
            auto localCamView = registry.view<components::CameraComponent, components::TransformComponent>();
            for (auto entity : localCamView)
            {
                auto& camComp = localCamView.get<components::CameraComponent>(entity);
                if (camComp.isPrimary)
                {
                    cameraPos = localCamView.get<components::TransformComponent>(entity).position;
                    break;
                }
            }
        }

        // Run distance-based streaming decisions
        streamer.update(cameraPos, sectorManager, streamingActions);

        for (const auto& action : streamingActions)
        {
            if (action.isLoad)
            {
                handleSectorLoad(action.coord);
            }
            else
            {
                handleSectorUnload(action.coord);
            }
        }

        // Process per-frame entity loading budget
        entityLoader.update(*sceneGraph, worldDefinition.streamingConfig.maxEntitiesPerFrame);
    }

    bool WorldSectorServiceImpl::createWorld(const std::string& name, const std::string& filePath,
                                              const world::SectorConfig& sectorConfig,
                                              const world::SectorStreamingConfig& streamingConfig)
    {
        sectorManager.clear();
        sectorManager.setConfig(sectorConfig);

        worldDefinition = {};
        worldDefinition.name = name;
        worldDefinition.sectorConfig = sectorConfig;
        worldDefinition.streamingConfig = streamingConfig;

        streamer.setConfig(streamingConfig);
        streamer.setEnabled(true);

        worldMode = true;
        currentWorldPath = filePath;

        // Assign existing entities to sectors (skip terrain/water/IBL/camera — they have their own systems)
        auto& root = sceneGraph->GetRoot();
        for (auto& child : root.getChildren())
        {
            if (isManagedBySeparateSystem(child))
                continue;

            if (child.hasComponent<components::TransformComponent>())
            {
                const auto& transform = child.getComponent<components::TransformComponent>();
                uint64_t uuid = child.getUUID().getValue();
                sectorManager.assignEntityToSector(uuid, transform.position);
            }
        }

        return saveWorld(filePath);
    }

    bool WorldSectorServiceImpl::saveWorld(const std::string& filePath)
    {
        if (!worldMode)
        {
            vfLogError("Cannot save world: not in world mode");
            return false;
        }

        std::string path = filePath.empty() ? currentWorldPath : filePath;
        if (path.empty())
        {
            vfLogError("Cannot save world: no file path specified");
            return false;
        }

        // Save dirty sectors
        std::filesystem::path worldDir = std::filesystem::path(path).parent_path();
        std::filesystem::path sectorsDir = worldDir / "sectors";
        std::filesystem::create_directories(sectorsDir);

        sectorManager.forEachSector([&](world::WorldSector& sector)
        {
            if (sector.dirty || sector.filePath.empty())
            {
                std::string sectorFileName = "sector_" +
                    std::to_string(sector.coord.x) + "_" +
                    std::to_string(sector.coord.z) + ".vfsector";
                std::string sectorPath = (sectorsDir / sectorFileName).string();

                if (world::WorldSectorSerialization::saveSector(sector, *sceneGraph, sectorPath))
                {
                    sector.filePath = sectorPath;
                    worldDefinition.sectorFilePaths[sector.coord] = sectorPath;
                }
            }
        });

        currentWorldPath = path;
        return world::WorldDefinitionSerialization::save(worldDefinition, path);
    }

    bool WorldSectorServiceImpl::loadWorld(const std::string& filePath)
    {
        world::WorldDefinition newDef;
        if (!world::WorldDefinitionSerialization::load(filePath, newDef))
            return false;

        sectorManager.clear();
        sectorManager.setConfig(newDef.sectorConfig);

        worldDefinition = newDef;
        streamer.setConfig(newDef.streamingConfig);
        streamer.setEnabled(true);

        worldMode = true;
        currentWorldPath = filePath;

        // Register all known sectors as Unloaded
        for (const auto& [coord, sectorPath] : worldDefinition.sectorFilePaths)
        {
            auto& sector = sectorManager.getOrCreateSector(coord);
            sector.filePath = sectorPath;
            sector.state = world::SectorState::Unloaded;
        }

        ::events::world::WorldLoadedNotification notif;
        notif.worldPath = filePath;
        ::events::EventDispatcher::instance().publish(notif);

        return true;
    }

    bool WorldSectorServiceImpl::saveSector(const world::SectorCoord& coord, const std::string& filePath)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector)
        {
            vfLogError("Cannot save sector ({},{}): not found", coord.x, coord.z);
            return false;
        }

        bool result = world::WorldSectorSerialization::saveSector(*sector, *sceneGraph, filePath);
        if (result)
        {
            worldDefinition.sectorFilePaths[coord] = filePath;
        }
        return result;
    }

    bool WorldSectorServiceImpl::loadSector(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector || sector->filePath.empty())
        {
            vfLogError("Cannot load sector ({},{}): no file path", coord.x, coord.z);
            return false;
        }

        handleSectorLoad(coord);
        return true;
    }

    bool WorldSectorServiceImpl::unloadSector(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector || sector->state != world::SectorState::Loaded)
        {
            return false;
        }

        handleSectorUnload(coord);
        return true;
    }

    void WorldSectorServiceImpl::handleSectorLoad(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector || sector->filePath.empty())
            return;

        sector->state = world::SectorState::Loading;

        // Pre-read UUIDs from the sector file for tracking
        std::vector<nlohmann::json> entityData;
        if (!world::WorldSectorSerialization::loadSector(sector->filePath, entityData))
        {
            sector->state = world::SectorState::Unloaded;
            return;
        }

        sector->entityUUIDs.clear();
        for (const auto& data : entityData)
        {
            if (data.contains("uuid") && data["uuid"].is_number_unsigned())
            {
                sector->entityUUIDs.push_back(data["uuid"].get<uint64_t>());
            }
        }

        // Queue deferred entity loading (reads file again internally, budgeted per-frame)
        entityLoader.queueSectorLoad(coord, sector->filePath);

        sector->state = world::SectorState::Loaded;

        ::events::world::SectorLoadedNotification notif;
        notif.coord = coord;
        notif.entityCount = static_cast<uint32_t>(entityData.size());
        ::events::EventDispatcher::instance().publish(notif);

        referenceResolver.onSectorLoaded(sector->entityUUIDs);
    }

    void WorldSectorServiceImpl::handleSectorUnload(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector)
            return;

        sector->state = world::SectorState::Unloading;

        referenceResolver.onSectorUnloaded(sector->entityUUIDs);

        entityLoader.queueSectorUnload(coord, sector->entityUUIDs);

        sector->entityUUIDs.clear();
        sector->state = world::SectorState::Unloaded;

        ::events::world::SectorUnloadedNotification notif;
        notif.coord = coord;
        ::events::EventDispatcher::instance().publish(notif);
    }

    void WorldSectorServiceImpl::onTransformChanged(uint64_t uuid, const glm::vec3& newPosition)
    {
        world::SectorCoord oldCoord = sectorManager.getEntitySector(uuid);
        world::SectorCoord newCoord = sectorManager.worldPositionToSectorCoord(newPosition);

        if (oldCoord == newCoord)
            return;

        sectorManager.removeEntityFromSector(uuid, oldCoord);
        sectorManager.assignEntityToSector(uuid, newPosition);

        vfLogInfo("Entity {} migrated from sector ({},{}) to ({},{})",
                  uuid, oldCoord.x, oldCoord.z, newCoord.x, newCoord.z);
    }

} // namespace services
