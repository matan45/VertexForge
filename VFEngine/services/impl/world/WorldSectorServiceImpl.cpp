#include "WorldSectorServiceImpl.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/render/ObjectStreamingEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../data/EditorMode.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "resource/AssetLifecycleHelpers.hpp"
#include "print/Log.hpp"
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
            // Look up the entity's actual deserialized position for correct sector assignment
            glm::vec3 assignPos(
                (static_cast<float>(coord.x) + 0.5f) * sectorManager.getConfig().sectorWorldSize,
                0.0f,
                (static_cast<float>(coord.z) + 0.5f) * sectorManager.getConfig().sectorWorldSize
            );

            auto entity = scene::EntityRegistry::findByUUID(uuid);
            if (entity != entt::null)
            {
                scene::Entity sceneEntity(entity);
                if (sceneEntity.hasComponent<components::TransformComponent>())
                {
                    assignPos = sceneEntity.getComponent<components::TransformComponent>().position;
                }
            }

            sectorManager.assignEntityToSector(uuid, assignPos);
            // Entity was loaded from file — don't mark sector as needing save
            auto* sector = sectorManager.getSector(coord);
            if (sector) sector->dirty = false;
        });

        entityLoader.setOnEntityUnloaded([this](uint64_t uuid, const world::SectorCoord& coord)
        {
            sectorManager.removeEntityFromSector(uuid, coord);
        });

        entityLoader.setOnEntityPostLoad([](uint64_t uuid, const std::string& meshPath, const std::string& animatorPath)
        {
            auto entity = scene::EntityRegistry::findByUUID(uuid);
            if (entity != entt::null)
            {
                scene::Entity sceneEntity(entity);
                auto& lifecycle = resource::AssetLifecycleManager::instance();

                resource::acquireEntityAssets(sceneEntity, lifecycle);

                if (!meshPath.empty())
                {
                    ::events::scene::MeshDataChangedNotification meshNotif;
                    meshNotif.entity = internal::toHandle(entity);
                    meshNotif.meshPath = meshPath;
                    meshNotif.animatorPath = animatorPath;
                    ::events::EventDispatcher::instance().publish(meshNotif);
                }

                // Create physics body if entity has physics components (for play-mode streaming)
                if (sceneEntity.hasComponent<components::RigidBodyComponent>())
                {
                    const auto& rigidBody = sceneEntity.getComponent<components::RigidBodyComponent>();
                    const auto& transform = sceneEntity.getComponent<components::TransformComponent>();

                    ::events::physics::AddRigidBodyCommand cmd;
                    cmd.entity = internal::toHandle(entity);

                    switch (rigidBody.type)
                    {
                    case components::RigidBodyType::Static:   cmd.rigidBody.type = services::RigidBodyData::Type::Static; break;
                    case components::RigidBodyType::Kinematic: cmd.rigidBody.type = services::RigidBodyData::Type::Kinematic; break;
                    default:                                   cmd.rigidBody.type = services::RigidBodyData::Type::Dynamic; break;
                    }
                    cmd.rigidBody.mass = rigidBody.mass;
                    cmd.rigidBody.linearDamping = rigidBody.linearDamping;
                    cmd.rigidBody.angularDamping = rigidBody.angularDamping;

                    if (sceneEntity.hasComponent<components::ColliderComponent>())
                    {
                        const auto& collider = sceneEntity.getComponent<components::ColliderComponent>();
                        switch (collider.shape)
                        {
                        case components::ColliderShape::Box:          cmd.collider.shape = services::ColliderData::Shape::Box; break;
                        case components::ColliderShape::Sphere:       cmd.collider.shape = services::ColliderData::Shape::Sphere; break;
                        case components::ColliderShape::Capsule:      cmd.collider.shape = services::ColliderData::Shape::Capsule; break;
                        case components::ColliderShape::ConvexMesh:   cmd.collider.shape = services::ColliderData::Shape::ConvexMesh; break;
                        case components::ColliderShape::TriangleMesh: cmd.collider.shape = services::ColliderData::Shape::TriangleMesh; break;
                        }
                        cmd.collider.size = collider.size * glm::abs(transform.scale);
                        cmd.collider.height = collider.height;
                        cmd.collider.isTrigger = collider.isTrigger;
                        cmd.collider.offset = collider.offset;
                        cmd.collider.collisionLayer = collider.collisionLayer;
                        if (collider.meshRef.isValid())
                            cmd.collider.meshPath = collider.meshRef.resolve();
                        else if (!meshPath.empty())
                            cmd.collider.meshPath = meshPath;
                    }
                    else
                    {
                        cmd.collider.shape = services::ColliderData::Shape::Box;
                        cmd.collider.size = glm::abs(transform.scale);
                    }

                    ::events::EventDispatcher::instance().execute(cmd);
                }
                else if (sceneEntity.hasComponent<components::ColliderComponent>())
                {
                    const auto& collider = sceneEntity.getComponent<components::ColliderComponent>();
                    const auto& transform = sceneEntity.getComponent<components::TransformComponent>();

                    ::events::physics::AddColliderCommand cmd;
                    cmd.entity = internal::toHandle(entity);
                    switch (collider.shape)
                    {
                    case components::ColliderShape::Box:          cmd.collider.shape = services::ColliderData::Shape::Box; break;
                    case components::ColliderShape::Sphere:       cmd.collider.shape = services::ColliderData::Shape::Sphere; break;
                    case components::ColliderShape::Capsule:      cmd.collider.shape = services::ColliderData::Shape::Capsule; break;
                    case components::ColliderShape::ConvexMesh:   cmd.collider.shape = services::ColliderData::Shape::ConvexMesh; break;
                    case components::ColliderShape::TriangleMesh: cmd.collider.shape = services::ColliderData::Shape::TriangleMesh; break;
                    }
                    cmd.collider.size = collider.size * glm::abs(transform.scale);
                    cmd.collider.height = collider.height;
                    cmd.collider.isTrigger = collider.isTrigger;
                    cmd.collider.offset = collider.offset;
                    cmd.collider.collisionLayer = collider.collisionLayer;
                    if (collider.meshRef.isValid())
                        cmd.collider.meshPath = collider.meshRef.resolve();
                    else if (!meshPath.empty())
                        cmd.collider.meshPath = meshPath;

                    ::events::EventDispatcher::instance().execute(cmd);
                }
            }
        });

        // Publish EntityDeletedNotification before entity is destroyed so
        // AssetLifecycleServiceImpl releases all asset types (mesh, material, audio, VFX, animator).
        // SceneGraphSystem::removeEntity() doesn't publish this — only HierarchyService does.
        // Also remove physics bodies for streamed entities.
        entityLoader.setOnEntityPreDestroy([](uint64_t entityHandleId)
        {
            services::EntityHandle handle{ entityHandleId };

            ::events::physics::HasRigidBodyQuery hasBodyQuery;
            hasBodyQuery.entity = handle;
            auto hasBody = ::events::EventDispatcher::instance().query(hasBodyQuery);
            if (hasBody)
            {
                ::events::physics::RemoveRigidBodyCommand removeCmd;
                removeCmd.entity = handle;
                ::events::EventDispatcher::instance().execute(removeCmd);
            }

            ::events::scene::EntityDeletedNotification notif;
            notif.entity = handle;
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

        dispatcher.registerCommandHandler<::events::world::SetSectorDebugDrawCommand>(
            [this](const ::events::world::SetSectorDebugDrawCommand& cmd)
            {
                debugDrawSectors = cmd.enabled;
            });

        dispatcher.registerQueryHandler<::events::world::GetSectorDebugDrawQuery>(
            [this](const ::events::world::GetSectorDebugDrawQuery&)
            {
                return debugDrawSectors;
            });

        // Handle play/stop transitions — snapshot restore creates new entity handles
        editorModeChangedToken = dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& notif)
            {
                if (!worldMode) return;

                if (notif.currentMode == services::EditorMode::Play)
                {
                    isPlayMode = true;

                    // Entering play mode — simulate runtime: unload all sectors so they
                    // stream in based on camera distance (like a fresh world load)
                    savedWorldDefinition = worldDefinition;
                    savedWorldPath = currentWorldPath;
                    entityLoader.clear();

                    int loadedCount = 0;
                    int totalStaticEntities = 0;

                    // Remove static sector entities from scene; keep dynamic ones alive
                    sectorManager.forEachSector([&](world::WorldSector& sector)
                    {
                        if (sector.state != world::SectorState::Loaded || sector.entityUUIDs.empty())
                            return;

                        loadedCount++;
                        std::vector<uint64_t> staticUUIDs;

                        for (uint64_t uuid : sector.entityUUIDs)
                        {
                            bool isDynamic = false;
                            auto ent = scene::EntityRegistry::findByUUID(uuid);
                            if (ent != entt::null)
                            {
                                scene::Entity sceneEntity(ent);
                                if (sceneEntity.hasComponent<components::TransformComponent>())
                                    isDynamic = !sceneEntity.getComponent<components::TransformComponent>().isStatic;
                            }
                            if (!isDynamic)
                                staticUUIDs.push_back(uuid);
                        }

                        totalStaticEntities += static_cast<int>(staticUUIDs.size());
                        entityLoader.queueSectorUnload(sector.coord, staticUUIDs);
                    });

                    entityLoader.flush(*sceneGraph);

                    sectorManager.forEachSector([](world::WorldSector& sector)
                    {
                        sector.entityUUIDs.clear();
                        sector.state = world::SectorState::Unloaded;
                    });
                }
                else if (notif.currentMode == services::EditorMode::Edit)
                {
                    isPlayMode = false;

                    // Returning to edit mode — snapshot was restored, re-assign entities to sectors
                    entityLoader.clear();
                    sectorManager.clear();
                    sectorManager.setConfig(savedWorldDefinition.sectorConfig);
                    worldDefinition = savedWorldDefinition;
                    currentWorldPath = savedWorldPath;

                    for (const auto& [coord, sectorPath] : worldDefinition.sectorFilePaths)
                    {
                        auto& sector = sectorManager.getOrCreateSector(coord);
                        sector.filePath = sectorPath;
                        sector.state = world::SectorState::Unloaded;
                    }

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
            [this](const ::events::scene::SceneLoadedNotification& notif)
            {
                auto& root = sceneGraph->GetRoot();
                if (root.hasComponent<components::WorldSectorComponent>())
                {
                    const auto& wsComp = root.getComponent<components::WorldSectorComponent>();
                    if (!wsComp.worldFilePath.empty())
                    {
                        loadWorld(wsComp.worldFilePath);
                        return;
                    }
                }

                // Fallback: check for .vfworld file next to the scene file
                if (!notif.scenePath.empty())
                {
                    std::filesystem::path scenePath(notif.scenePath);
                    std::filesystem::path worldPath = scenePath.parent_path() / (scenePath.stem().string() + ".vfworld");
                    if (std::filesystem::exists(worldPath))
                    {
                        std::string wp = worldPath.string();
                        loadWorld(wp);

                        // Tag root so future saves include it
                        root.addOrReplaceComponent<components::WorldSectorComponent>().worldFilePath = wp;
                    }
                }
            });

        // Track dynamically-created entities in world mode
        entityCreatedToken = dispatcher.subscribe<::events::scene::EntityCreatedNotification>(
            [this](const ::events::scene::EntityCreatedNotification& notif)
            {
                if (!worldMode) return;

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(notif.entity);
                if (!registry.valid(entity)) return;

                scene::Entity sceneEntity(entity);
                if (isManagedBySeparateSystem(sceneEntity)) return;

                if (sceneEntity.hasComponent<components::TransformComponent>())
                {
                    uint64_t uuid = sceneEntity.getUUID().getValue();
                    if (!sectorManager.hasEntitySector(uuid))
                    {
                        const auto& transform = sceneEntity.getComponent<components::TransformComponent>();
                        sectorManager.assignEntityToSector(uuid, transform.position);
                    }
                }
            });

        sceneClearedToken = dispatcher.subscribe<::events::scene::SceneClearedNotification>(
            [this](const ::events::scene::SceneClearedNotification&)
            {
                if (worldMode)
                {
                    // Disable GPU object streaming
                    events::render::objectstreaming::SetObjectStreamingEnabledCommand cmd;
                    cmd.enabled = false;
                    ::events::EventDispatcher::instance().execute(cmd);

                    entityLoader.clear();
                    sectorManager.clear();
                    worldDefinition = {};
                    streamer.setEnabled(false);
                    worldMode = false;
                    currentWorldPath.clear();
                }
            });

        terrainCreatedToken = dispatcher.subscribe<::events::terrain::TerrainCreatedNotification>(
            [this](const ::events::terrain::TerrainCreatedNotification& notif)
            {
                onTerrainAvailable(notif.config.worldTileSize);
            });

        terrainLoadedToken = dispatcher.subscribe<::events::terrain::TerrainLoadedNotification>(
            [this](const ::events::terrain::TerrainLoadedNotification&)
            {
                float tileSize = ::events::EventDispatcher::instance().query(
                    ::events::terrain::GetActiveTerrainTileSizeQuery{});
                if (tileSize > 0.0f)
                {
                    onTerrainAvailable(tileSize);
                }
            });
    }

    void WorldSectorServiceImpl::onTerrainAvailable(float worldTileSize)
    {
        if (!worldMode || worldTileSize <= 0.0f) return;

        const auto& config = sectorManager.getConfig();
        if (config.alignedToTerrain)
        {
            float expected = worldTileSize * static_cast<float>(config.tilesPerSector);
            if (!world::isSectorAlignedToTerrain(config, worldTileSize))
            {
                vfLogWarning("Sector grid misaligned with terrain (sector={}, expected={}). Updating.",
                          config.sectorWorldSize, expected);

                world::SectorConfig newConfig = config;
                newConfig.sectorWorldSize = expected;
                sectorManager.setConfig(newConfig);
                worldDefinition.sectorConfig = newConfig;
            }
        }
        else
        {
            if (!world::isSectorAlignedToTerrain(config, worldTileSize))
            {
                vfLogWarning("Sector grid not aligned to terrain. sectorWorldSize={} but tilesPerSector*worldTileSize={}.",
                          config.sectorWorldSize,
                          worldTileSize * static_cast<float>(config.tilesPerSector));
            }
        }
    }

} // namespace services
