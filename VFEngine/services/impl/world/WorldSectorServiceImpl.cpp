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
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../data/EditorMode.hpp"
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
            // Find the entity by UUID and acquire all its assets
            auto& registry = scene::EntityRegistry::getRegistry();
            auto uuidView = registry.view<components::UUIDComponent>();
            for (auto entity : uuidView)
            {
                if (uuidView.get<components::UUIDComponent>(entity).id.getValue() == uuid)
                {
                    scene::Entity sceneEntity(entity);
                    auto& lifecycle = resource::AssetLifecycleManager::instance();

                    // Mesh + animator
                    if (sceneEntity.hasComponent<components::MeshComponent>())
                    {
                        const auto& mesh = sceneEntity.getComponent<components::MeshComponent>();
                        if (!mesh.meshPath.empty()) lifecycle.acquire(mesh.meshPath, resource::AssetType::Mesh);
                        if (!mesh.animatorPath.empty()) lifecycle.acquire(mesh.animatorPath, resource::AssetType::Animator);
                    }

                    // Materials
                    if (sceneEntity.hasComponent<components::MaterialComponent>())
                    {
                        const auto& mat = sceneEntity.getComponent<components::MaterialComponent>();
                        if (!mat.defaultMaterial.empty()) lifecycle.acquire(mat.defaultMaterial, resource::AssetType::Material);
                        for (const auto& [name, path] : mat.subMeshMaterials)
                        {
                            if (!path.empty()) lifecycle.acquire(path, resource::AssetType::Material);
                        }
                    }

                    // Audio 2D
                    if (sceneEntity.hasComponent<components::AudioSource2DComponent>())
                    {
                        const auto& audio = sceneEntity.getComponent<components::AudioSource2DComponent>();
                        if (!audio.audioFilePath.empty()) lifecycle.acquire(audio.audioFilePath, resource::AssetType::Audio);
                    }

                    // Audio 3D
                    if (sceneEntity.hasComponent<components::AudioSource3DComponent>())
                    {
                        const auto& audio = sceneEntity.getComponent<components::AudioSource3DComponent>();
                        if (!audio.audioFilePath.empty()) lifecycle.acquire(audio.audioFilePath, resource::AssetType::Audio);
                    }

                    // VFX
                    if (sceneEntity.hasComponent<components::VFXComponent>())
                    {
                        const auto& vfx = sceneEntity.getComponent<components::VFXComponent>();
                        if (!vfx.vfxPath.empty()) lifecycle.acquire(vfx.vfxPath, resource::AssetType::VFX);
                    }

                    // Animator (standalone)
                    if (sceneEntity.hasComponent<components::AnimatorComponent>())
                    {
                        const auto& anim = sceneEntity.getComponent<components::AnimatorComponent>();
                        if (!anim.animatorPath.empty()) lifecycle.acquire(anim.animatorPath, resource::AssetType::Animator);
                    }

                    // Publish mesh notification so rendering picks it up
                    if (!meshPath.empty())
                    {
                        ::events::scene::MeshDataChangedNotification meshNotif;
                        meshNotif.entity = internal::toHandle(entity);
                        meshNotif.meshPath = meshPath;
                        meshNotif.animatorPath = animatorPath;
                        ::events::EventDispatcher::instance().publish(meshNotif);
                    }

                    break;
                }
            }
        });

        // Publish EntityDeletedNotification before entity is destroyed so
        // AssetLifecycleServiceImpl releases all asset types (mesh, material, audio, VFX, animator).
        // SceneGraphSystem::removeEntity() doesn't publish this — only HierarchyService does.
        entityLoader.setOnEntityPreDestroy([](uint64_t entityHandleId)
        {
            ::events::scene::EntityDeletedNotification notif;
            notif.entity = services::EntityHandle{ entityHandleId };
            ::events::EventDispatcher::instance().publish(notif);
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

        dispatcher.registerCommandHandler<::events::world::ClearWorldCommand>(
            [this](const ::events::world::ClearWorldCommand&)
            {
                clearWorld();
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

        dispatcher.registerQueryHandler<::events::world::DoesSectorExistQuery>(
            [this](const ::events::world::DoesSectorExistQuery& q)
            {
                const auto* sector = sectorManager.getSector(q.coord);
                return sector != nullptr && !sector->filePath.empty();
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

        // Handle play/stop transitions — snapshot restore creates new entity handles
        editorModeChangedToken = dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& notif)
            {
                if (!worldMode) return;

                if (notif.currentMode == services::EditorMode::Play)
                {
                    // Entering play mode — save world state, clear sector tracking
                    savedWorldDefinition = worldDefinition;
                    savedWorldPath = currentWorldPath;
                    entityLoader.clear();
                    sectorManager.clear();
                }
                else if (notif.currentMode == services::EditorMode::Edit)
                {
                    // Returning to edit mode — snapshot was restored, re-assign entities to sectors
                    entityLoader.clear();
                    sectorManager.clear();
                    sectorManager.setConfig(savedWorldDefinition.sectorConfig);
                    worldDefinition = savedWorldDefinition;
                    currentWorldPath = savedWorldPath;

                    // Re-register known sectors
                    for (const auto& [coord, sectorPath] : worldDefinition.sectorFilePaths)
                    {
                        auto& sector = sectorManager.getOrCreateSector(coord);
                        sector.filePath = sectorPath;
                        sector.state = world::SectorState::Unloaded;
                    }

                    // Re-assign live entities to their sectors
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

                    // Mark sectors that have entities as Loaded
                    sectorManager.forEachSector([](world::WorldSector& sector)
                    {
                        if (!sector.entityUUIDs.empty())
                        {
                            sector.state = world::SectorState::Loaded;
                        }
                    });
                }
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

        // Cache camera position from viewport (editor camera isn't in ECS)
        cameraPositionToken = dispatcher.subscribe<::events::render::CameraPositionUpdatedNotification>(
            [this](const ::events::render::CameraPositionUpdatedNotification& notif)
            {
                cachedCameraPos = notif.position;
            });

        // Auto-load world when a scene with WorldSectorComponent is loaded
        sceneLoadedToken = dispatcher.subscribe<::events::scene::SceneLoadedNotification>(
            [this](const ::events::scene::SceneLoadedNotification&)
            {
                auto& root = sceneGraph->GetRoot();
                if (root.hasComponent<components::WorldSectorComponent>())
                {
                    const auto& wsComp = root.getComponent<components::WorldSectorComponent>();
                    if (!wsComp.worldFilePath.empty())
                    {
                        loadWorld(wsComp.worldFilePath);
                    }
                }
            });

        // Clear world state when scene is cleared (new scene)
        sceneClearedToken = dispatcher.subscribe<::events::scene::SceneClearedNotification>(
            [this](const ::events::scene::SceneClearedNotification&)
            {
                if (worldMode)
                {
                    entityLoader.clear();
                    sectorManager.clear();
                    worldDefinition = {};
                    streamer.setEnabled(false);
                    worldMode = false;
                    currentWorldPath.clear();
                }
            });
    }

    void WorldSectorServiceImpl::update()
    {
        if (!worldMode)
            return;

        // Use cached camera position (updated by CameraPositionUpdatedNotification from viewport)
        streamer.update(cachedCameraPos, sectorManager, streamingActions);

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

        // Tag root entity so the world auto-loads with the scene
        auto& root = sceneGraph->GetRoot();
        root.addOrReplaceComponent<components::WorldSectorComponent>().worldFilePath = filePath;

        // Assign existing entities to sectors (skip terrain/water/IBL/camera — they have their own systems)
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

        // Mark all sectors as Loaded since entities are already live in the scene
        sectorManager.forEachSector([](world::WorldSector& sector)
        {
            sector.state = world::SectorState::Loaded;
        });

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

    void WorldSectorServiceImpl::clearWorld()
    {
        if (!worldMode)
            return;

        // Clear pending load/unload queues
        entityLoader.clear();

        // Clear sector manager (entities remain in scene, just no longer tracked by sectors)
        sectorManager.clear();

        // Remove WorldSectorComponent from root so scene won't auto-load world next time
        auto& root = sceneGraph->GetRoot();
        if (root.hasComponent<components::WorldSectorComponent>())
        {
            root.removeComponent<components::WorldSectorComponent>();
        }

        worldDefinition = {};
        streamer.setEnabled(false);
        worldMode = false;
        currentWorldPath.clear();
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
