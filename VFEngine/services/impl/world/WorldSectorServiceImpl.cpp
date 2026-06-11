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
#include "../../events/vfx/VFXSnapshotEvents.hpp"
#include "../../events/audio/AudioSnapshotEvents.hpp"
#include "../../events/world/HLODEvents.hpp"
#include "world/HLODGenerator.hpp"
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
            || entity.hasComponent<components::OceanComponent>()
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

        entityLoader.setOnEntityPostLoad([this](uint64_t uuid, const std::string& meshPath, const std::string& animatorPath)
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

                    // Restore velocity and sleep state from snapshot if available
                    auto snapIt = physicsSnapshots.find(uuid);
                    if (snapIt != physicsSnapshots.end())
                    {
                        cmd.rigidBody.linearVelocity = snapIt->second.linearVelocity;
                        cmd.rigidBody.angularVelocity = snapIt->second.angularVelocity;
                        cmd.rigidBody.activateOnAdd = !snapIt->second.wasSleeping;
                        physicsSnapshots.erase(snapIt);
                    }

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
                        cmd.collider.submeshIndex = collider.submeshIndex;
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
                    cmd.collider.submeshIndex = collider.submeshIndex;
                    if (collider.meshRef.isValid())
                        cmd.collider.meshPath = collider.meshRef.resolve();
                    else if (!meshPath.empty())
                        cmd.collider.meshPath = meshPath;

                    ::events::EventDispatcher::instance().execute(cmd);
                }

                // Queue animation state restore (applied after animator init in RuntimeAnimatorSystem)
                auto animSnapIt = animationSnapshots.find(uuid);
                if (animSnapIt != animationSnapshots.end())
                {
                    ::events::animation::snapshot::RestoreAnimationSnapshotCommand animCmd;
                    animCmd.entity = internal::toHandle(entity);
                    animCmd.snapshot = std::move(animSnapIt->second);
                    ::events::EventDispatcher::instance().execute(animCmd);
                    animationSnapshots.erase(animSnapIt);
                }

                // Restore audio playback state from snapshot
                auto audioSnapIt = audioSnapshots.find(uuid);
                if (audioSnapIt != audioSnapshots.end())
                {
                    const auto& snap = audioSnapIt->second;
                    if (snap.wasPlaying && snap.loop && !snap.audioPath.empty())
                    {
                        auto& disp = ::events::EventDispatcher::instance();
                        uint64_t newHandle = 0;

                        if (snap.is3D && sceneEntity.hasComponent<components::AudioSource3DComponent>())
                        {
                            auto& audioComp = sceneEntity.getComponent<components::AudioSource3DComponent>();
                            const auto& transform = sceneEntity.getComponent<components::TransformComponent>();

                            ::events::audio::snapshot::PlayRestoredAudio3DCommand playCmd;
                            playCmd.path = snap.audioPath;
                            playCmd.position = transform.position;
                            playCmd.volume = snap.volume;
                            playCmd.pitch = snap.pitch;
                            playCmd.loop = snap.loop;
                            playCmd.minDistance = audioComp.minDistance;
                            playCmd.maxDistance = audioComp.maxDistance;
                            playCmd.busName = snap.busName;
                            newHandle = disp.execute(playCmd);

                            audioComp.activeHandle = newHandle;
                            audioComp.isPlaying = true;
                        }
                        else if (!snap.is3D && sceneEntity.hasComponent<components::AudioSource2DComponent>())
                        {
                            auto& audioComp = sceneEntity.getComponent<components::AudioSource2DComponent>();

                            ::events::audio::snapshot::PlayRestoredAudio2DCommand playCmd;
                            playCmd.path = snap.audioPath;
                            playCmd.volume = snap.volume;
                            playCmd.pitch = snap.pitch;
                            playCmd.loop = snap.loop;
                            playCmd.busName = snap.busName;
                            newHandle = disp.execute(playCmd);

                            audioComp.activeHandle = newHandle;
                            audioComp.isPlaying = true;
                        }

                        if (newHandle != 0 && snap.playbackPosition > 0.0f)
                        {
                            ::events::audio::snapshot::SeekAudioCommand seekCmd;
                            seekCmd.handleId = newHandle;
                            seekCmd.seconds = snap.playbackPosition;
                            ::events::EventDispatcher::instance().execute(seekCmd);
                        }
                    }
                    audioSnapshots.erase(audioSnapIt);
                }
            }
        });

        // Publish EntityDeletedNotification before entity is destroyed so
        // AssetLifecycleServiceImpl releases all asset types (mesh, material, audio, VFX, animator).
        // SceneGraphSystem::removeEntity() doesn't publish this — only HierarchyService does.
        // Also remove physics bodies for streamed entities.
        entityLoader.setOnEntityPreDestroy([this](uint64_t entityHandleId)
        {
            services::EntityHandle handle{ entityHandleId };
            auto& dispatcher = ::events::EventDispatcher::instance();

            ::events::physics::HasRigidBodyQuery hasBodyQuery;
            hasBodyQuery.entity = handle;
            auto hasBody = dispatcher.query(hasBodyQuery);
            if (hasBody)
            {
                // Capture velocity and sleep state for dynamic bodies before removal
                auto ent = static_cast<entt::entity>(static_cast<uint32_t>(entityHandleId));
                auto& registry = scene::EntityRegistry::getRegistry();
                if (registry.valid(ent) && registry.any_of<components::RigidBodyComponent>(ent))
                {
                    const auto& rb = registry.get<components::RigidBodyComponent>(ent);
                    if (rb.type == components::RigidBodyType::Dynamic)
                    {
                        auto* uuidComp = registry.try_get<components::UUIDComponent>(ent);
                        if (uuidComp)
                        {
                            PhysicsSnapshot snap;

                            ::events::physics::GetLinearVelocityQuery linVelQuery;
                            linVelQuery.entity = handle;
                            snap.linearVelocity = dispatcher.query(linVelQuery);

                            ::events::physics::GetAngularVelocityQuery angVelQuery;
                            angVelQuery.entity = handle;
                            snap.angularVelocity = dispatcher.query(angVelQuery);

                            ::events::physics::IsBodySleepingQuery sleepQuery;
                            sleepQuery.entity = handle;
                            snap.wasSleeping = dispatcher.query(sleepQuery);

                            physicsSnapshots[uuidComp->id.getValue()] = snap;
                        }
                    }
                }

                ::events::physics::RemoveRigidBodyCommand removeCmd;
                removeCmd.entity = handle;
                dispatcher.execute(removeCmd);
            }

            // Capture animation state before entity destruction
            {
                ::events::animation::snapshot::CaptureAnimationSnapshotQuery animQuery;
                animQuery.entity = handle;
                auto animSnap = dispatcher.query(animQuery);
                if (animSnap.has_value())
                {
                    auto ent2 = static_cast<entt::entity>(static_cast<uint32_t>(entityHandleId));
                    auto& reg = scene::EntityRegistry::getRegistry();
                    auto* uc = reg.try_get<components::UUIDComponent>(ent2);
                    if (uc)
                        animationSnapshots[uc->id.getValue()] = std::move(*animSnap);
                }
            }

            // Capture VFX playback state before entity destruction
            {
                auto ent3 = static_cast<entt::entity>(static_cast<uint32_t>(entityHandleId));
                auto& reg = scene::EntityRegistry::getRegistry();
                if (reg.valid(ent3) && reg.any_of<components::VFXComponent>(ent3))
                {
                    const auto& vfxComp = reg.get<components::VFXComponent>(ent3);
                    if (vfxComp.runtimeInstanceId != 0)
                    {
                        ::events::vfx::snapshot::CaptureVFXSnapshotQuery vfxQuery;
                        vfxQuery.instanceId = vfxComp.runtimeInstanceId;
                        auto vfxSnap = dispatcher.query(vfxQuery);
                        if (vfxSnap.has_value())
                        {
                            auto* uc = reg.try_get<components::UUIDComponent>(ent3);
                            if (uc)
                                vfxSnapshots[uc->id.getValue()] = {
                                    vfxSnap->emissionTime, vfxSnap->spawnAccumulator,
                                    vfxSnap->wasPlaying, vfxSnap->wasActive};
                        }
                    }
                }
            }

            // Capture audio state and fade-out before entity destruction
            {
                auto ent4 = static_cast<entt::entity>(static_cast<uint32_t>(entityHandleId));
                auto& reg4 = scene::EntityRegistry::getRegistry();

                auto captureAudio = [&](auto& audioComp, bool is3D)
                {
                    if (audioComp.activeHandle == 0)
                        return;
                    auto* uc4 = reg4.try_get<components::UUIDComponent>(ent4);
                    if (!uc4)
                        return;

                    AudioSnapshot snap;
                    snap.is3D = is3D;
                    snap.volume = audioComp.volume;
                    snap.pitch = audioComp.pitch;
                    snap.loop = audioComp.loop;
                    snap.busName = audioComp.busName;
                    if (audioComp.audioRef.isValid())
                        snap.audioPath = audioComp.audioRef.resolve();

                    ::events::audio::snapshot::GetAudioPlaybackPositionQuery posQuery;
                    posQuery.handleId = audioComp.activeHandle;
                    snap.playbackPosition = dispatcher.query(posQuery);

                    ::events::audio::snapshot::IsAudioPlayingQuery playQuery;
                    playQuery.handleId = audioComp.activeHandle;
                    snap.wasPlaying = dispatcher.query(playQuery);

                    audioSnapshots[uc4->id.getValue()] = std::move(snap);

                    // Fade-out instead of hard stop
                    ::events::audio::snapshot::FadeOutAudioCommand fadeCmd;
                    fadeCmd.handleId = audioComp.activeHandle;
                    fadeCmd.fadeDurationMs = 300.0f;
                    dispatcher.execute(fadeCmd);

                    audioComp.activeHandle = 0;
                    audioComp.isPlaying = false;
                };

                if (reg4.valid(ent4))
                {
                    if (reg4.all_of<components::AudioSource3DComponent>(ent4))
                        captureAudio(reg4.get<components::AudioSource3DComponent>(ent4), true);
                    if (reg4.all_of<components::AudioSource2DComponent>(ent4))
                        captureAudio(reg4.get<components::AudioSource2DComponent>(ent4), false);
                }
            }

            ::events::scene::EntityDeletedNotification notif;
            notif.entity = handle;
            dispatcher.publish(notif);
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

        dispatcher.registerQueryHandler<::events::world::GetSectorConfigQuery>(
            [this](const ::events::world::GetSectorConfigQuery&)
            {
                return sectorManager.getConfig();
            });

        dispatcher.registerQueryHandler<::events::world::GetLoadedSectorCoordsQuery>(
            [this](const ::events::world::GetLoadedSectorCoordsQuery&)
            {
                std::vector<world::SectorCoord> result;
                sectorManager.forEachSector([&](const world::WorldSector& sector)
                {
                    if (sector.state == world::SectorState::Loaded ||
                        sector.state == world::SectorState::Loading)
                    {
                        result.push_back(sector.coord);
                    }
                });
                return result;
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

        dispatcher.registerCommandHandler<::events::world::MarkEntitySectorDirtyCommand>(
            [this](const ::events::world::MarkEntitySectorDirtyCommand& cmd)
            {
                // Edit-mode only: play-mode dirty sectors are pinned (never auto-unloaded)
                if (!worldMode || isPlayMode) return;
                if (!sectorManager.hasEntitySector(cmd.entityUUID)) return;

                auto coord = sectorManager.getEntitySector(cmd.entityUUID);
                if (auto* sector = sectorManager.getSector(coord))
                    sector->dirty = true;
            });

        dispatcher.registerCommandHandler<::events::world::RegisterStreamingSourceCommand>(
            [this](const ::events::world::RegisterStreamingSourceCommand& cmd) -> uint32_t
            {
                uint32_t id = nextStreamingSourceId++;
                world::StreamingSource source;
                source.position = cmd.position;
                source.radiusMultiplier = cmd.radiusMultiplier;
                source.priority = cmd.priority;
                source.id = id;
                streamingSources[id] = source;
                if (cmd.ownerEntityUUID != 0)
                    streamingSourceOwners[id] = cmd.ownerEntityUUID;
                return id;
            });

        dispatcher.registerCommandHandler<::events::world::UnregisterStreamingSourceCommand>(
            [this](const ::events::world::UnregisterStreamingSourceCommand& cmd)
            {
                streamingSources.erase(cmd.sourceId);
                streamingSourceOwners.erase(cmd.sourceId);
            });

        dispatcher.registerCommandHandler<::events::world::UpdateStreamingSourcePositionCommand>(
            [this](const ::events::world::UpdateStreamingSourcePositionCommand& cmd)
            {
                auto it = streamingSources.find(cmd.sourceId);
                if (it != streamingSources.end())
                    it->second.position = cmd.position;
            });

        dispatcher.registerQueryHandler<::events::world::IsStreamingSourceValidQuery>(
            [this](const ::events::world::IsStreamingSourceValidQuery& query) -> bool
            {
                return streamingSources.contains(query.sourceId);
            });

        // HLOD commands/queries
        dispatcher.registerCommandHandler<::events::world::hlod::GenerateHLODCommand>(
            [this](const ::events::world::hlod::GenerateHLODCommand& cmd) -> bool
            {
                if (!worldMode) return false;
                const auto* sector = sectorManager.getSector(cmd.coord);
                if (!sector || sector->filePath.empty()) return false;

                auto& tiers = worldDefinition.hlodConfig.tiers;
                world::HLODTierConfig tierConfig;
                for (const auto& t : tiers)
                {
                    if (t.tier == cmd.tier) { tierConfig = t; break; }
                }

                // Generate output path alongside sector file
                std::string hlodPath = sector->filePath;
                auto dotPos = hlodPath.rfind('.');
                if (dotPos != std::string::npos)
                    hlodPath = hlodPath.substr(0, dotPos);
                hlodPath += "_hlod" + std::to_string(cmd.tier) + ".vfHLOD";

                world::HLODGenerator generator;
                std::string workingDir = currentWorldPath.empty() ? "." :
                    currentWorldPath.substr(0, currentWorldPath.find_last_of("/\\"));

                bool result = generator.generateForSector(
                    cmd.coord, sector->filePath, workingDir,
                    tierConfig, hlodPath);

                if (result)
                {
                    sectorManager.getSector(cmd.coord)->hlodFilePath = hlodPath;
                }

                return result;
            });

        dispatcher.registerCommandHandler<::events::world::hlod::SetHLODConfigCommand>(
            [this](const ::events::world::hlod::SetHLODConfigCommand& cmd)
            {
                worldDefinition.hlodConfig = cmd.config;
                hlodStreamer.setConfig(worldDefinition.streamingConfig, worldDefinition.hlodConfig);
            });

        dispatcher.registerQueryHandler<::events::world::hlod::GetHLODConfigQuery>(
            [this](const ::events::world::hlod::GetHLODConfigQuery&)
            {
                return worldDefinition.hlodConfig;
            });

        dispatcher.registerQueryHandler<::events::world::hlod::IsHLODGeneratedQuery>(
            [this](const ::events::world::hlod::IsHLODGeneratedQuery& query) -> bool
            {
                const auto* sector = sectorManager.getSector(query.coord);
                return sector && !sector->hlodFilePath.empty();
            });

        dispatcher.registerCommandHandler<::events::world::hlod::GenerateAllHLODCommand>(
            [this](const ::events::world::hlod::GenerateAllHLODCommand&) -> bool
            {
                if (!worldMode) return false;
                bool allSuccess = true;
                sectorManager.forEachSector([&](world::WorldSector& sector)
                {
                    if (sector.filePath.empty()) return;

                    auto& tiers = worldDefinition.hlodConfig.tiers;
                    world::HLODTierConfig tierConfig;
                    if (!tiers.empty()) tierConfig = tiers[0];

                    std::string hlodPath = sector.filePath;
                    auto dotPos = hlodPath.rfind('.');
                    if (dotPos != std::string::npos)
                        hlodPath = hlodPath.substr(0, dotPos);
                    hlodPath += "_hlod0.vfHLOD";

                    std::string workingDir = currentWorldPath.empty() ? "." :
                        currentWorldPath.substr(0, currentWorldPath.find_last_of("/\\"));

                    world::HLODGenerator generator;
                    if (generator.generateForSector(sector.coord, sector.filePath, workingDir, tierConfig, hlodPath))
                        sector.hlodFilePath = hlodPath;
                    else
                        allSuccess = false;
                });
                return allSuccess;
            });

        dispatcher.registerCommandHandler<::events::world::hlod::InvalidateHLODCommand>(
            [this](const ::events::world::hlod::InvalidateHLODCommand& cmd)
            {
                invalidateHLODForSector(cmd.coord);
            });

        dispatcher.registerQueryHandler<::events::vfx::snapshot::GetVFXSnapshotQuery>(
            [this](const ::events::vfx::snapshot::GetVFXSnapshotQuery& query)
                -> std::optional<::events::vfx::snapshot::VFXPlaybackSnapshot>
            {
                auto it = vfxSnapshots.find(query.entityUUID);
                if (it == vfxSnapshots.end())
                    return std::nullopt;
                ::events::vfx::snapshot::VFXPlaybackSnapshot snap;
                snap.emissionTime = it->second.emissionTime;
                snap.spawnAccumulator = it->second.spawnAccumulator;
                snap.wasPlaying = it->second.wasPlaying;
                snap.wasActive = it->second.wasActive;
                vfxSnapshots.erase(it);
                return snap;
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
                    physicsSnapshots.clear();
                    animationSnapshots.clear();
                    vfxSnapshots.clear();
                    audioSnapshots.clear();
                    streamingSources.clear();
                    streamingSourceOwners.clear();
                    nextStreamingSourceId = 1;

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

        // Auto-unregister streaming sources whose owning entity is deleted
        entityDeletedToken = dispatcher.subscribe<::events::scene::EntityDeletedNotification>(
            [this](const ::events::scene::EntityDeletedNotification& notif)
            {
                if (!worldMode || streamingSourceOwners.empty()) return;

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(notif.entity);
                if (!registry.valid(entity)) return;

                auto* uuidComp = registry.try_get<components::UUIDComponent>(entity);
                if (!uuidComp) return;
                uint64_t uuid = uuidComp->id.getValue();

                auto it = streamingSourceOwners.begin();
                while (it != streamingSourceOwners.end())
                {
                    if (it->second == uuid)
                    {
                        streamingSources.erase(it->first);
                        it = streamingSourceOwners.erase(it);
                    }
                    else
                        ++it;
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

    void WorldSectorServiceImpl::invalidateHLODForSector(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector || sector->hlodFilePath.empty())
            return;

        // Delete the stale bake so loadWorld's disk probe doesn't resurrect it
        std::error_code ec;
        std::filesystem::remove(sector->hlodFilePath, ec);
        sector->hlodFilePath.clear();

        // Drop any loaded proxy covering this sector, per tier (real or future
        // geometry replaces it; the streamer re-emits a load once a new bake exists)
        auto floorDiv = [](int32_t v, int32_t s) { return (v >= 0) ? v / s : (v - s + 1) / s; };
        for (const auto& tier : worldDefinition.hlodConfig.tiers)
        {
            int32_t cs = static_cast<int32_t>(tier.cellSize);
            world::HLODCellCoord cell(floorDiv(coord.x, cs), floorDiv(coord.z, cs), tier.tier);
            hlodProxyManager.unloadProxy(cell, *sceneGraph);
            hlodStreamer.forgetProxy(cell);
        }

        ::events::world::hlod::HLODInvalidatedNotification notif;
        notif.coord = coord;
        ::events::EventDispatcher::instance().publish(notif);

        vfLogInfo("HLOD invalidated for sector [{},{}]", coord.x, coord.z);
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
