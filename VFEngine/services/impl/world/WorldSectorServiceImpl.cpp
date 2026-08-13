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
#include "world/HLODCellPlanner.hpp"
#include "world/SectorAssignment.hpp"
#include "../../data/EntityConversion.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "resource/AssetLifecycleHelpers.hpp"
#include "resource/VirtualFileSystem.hpp"
#include "../common/ProjectPaths.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace services
{
    WorldSectorServiceImpl::WorldSectorServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph))
    {
        // VK-1594: the baker generates on workers but publishes here, on the main-thread-pinned
        // update(), so recordHLODBake is the only writer of worldDefinition.hlodCells.
        hlodBaker.setCellBakedCallback(
            [this](const world::HLODCellCoord& cell, const std::string& outputPath)
            {
                recordHLODBake(cell, outputPath);

                ::events::world::hlod::HLODGenerationCompleteNotification notif;
                notif.coord = cell;
                notif.success = true;
                ::events::EventDispatcher::instance().publish(notif);
            });

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

                // VK-1597: a .vfsector written before the entity was pinned still lists it. Spawn
                // it (the payload is the only copy of its data) but do NOT re-capture it into the
                // sector, or the very next unload would destroy it again. The next Save World
                // rewrites the file without it - see reconcileAlwaysLoadedEntities.
                if (!world::readEntityStreamingTraits(sceneEntity).spatiallyLoaded)
                    return;

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

                // VK-1590: register this entity's outgoing cross-sector references. The subtree
                // walk is required because sector payloads nest their children in the entity
                // JSON, while this callback fires only for the payload root.
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto registerSubtree = [&](auto&& self, entt::entity ent) -> void
                    {
                        if (ent == entt::null || !registry.valid(ent)) return;

                        if (const auto* uuidComp = registry.try_get<components::UUIDComponent>(ent))
                        {
                            world::SectorRefFieldRegistry::registerEntityReferences(
                                registry, ent, uuidComp->id.getValue(),
                                referenceResolver, refTargetProbeQueue);
                        }

                        if (const auto* childrenComp = registry.try_get<components::ChildrenComponent>(ent))
                        {
                            for (auto child : childrenComp->children)
                            {
                                self(self, child);
                            }
                        }
                    };
                    registerSubtree(registerSubtree, entity);
                }

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
                            // VK-1513: sourced from the live component, like the distances
                            // above — the component outlives the sector's audio playback.
                            playCmd.priority = audioComp.priority;
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
                    // VK-1520: this captures the AUTHORED pitch/clip, not the variant
                    // and jitter the voice is actually playing. Restoring is gated on
                    // `wasPlaying && loop` below, and variation containers target
                    // one-shots (loop == false), so it is unreachable in practice —
                    // a looped Random container would resume on variant 0 at the
                    // authored pitch after a sector reload.
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

    void WorldSectorServiceImpl::applyEffectiveStreamingConfig()
    {
        const auto& effective = getEffectiveStreamingConfig();
        streamer.setConfig(effective);
        // Take the config back off the streamer: setConfig normalizes, and the HLOD streamer must
        // see the same resolved radii the sector streamer is running.
        hlodStreamer.setConfig(streamer.getConfig(), worldDefinition.hlodConfig);
    }

    void WorldSectorServiceImpl::resetStreamingSessionState()
    {
        streamingConfigOverride.reset();
        streamer.setPaused(false);
    }

    ::events::world::StreamingOverlaySnapshot
    WorldSectorServiceImpl::buildOverlaySnapshot(int32_t maxRadius)
    {
        ::events::world::StreamingOverlaySnapshot snapshot;
        if (!worldMode)
            return snapshot; // valid == false; the overlay draws a "no world" line instead

        // The STREAMER's copy, not getEffectiveStreamingConfig(). Both name the same config, but
        // only this one has been through normalizeConfig: worldDefinition.streamingConfig is
        // whatever loadWorld read off disk (nothing validates a .vfworld) or whatever a script
        // handed CreateWorldCommand. A world whose file says unloadRadius 5 with loadRadius 8
        // actually evicts at 9, and drawing the raw 5 would put the red unload ring INSIDE the
        // green load ring and size the grid too small to contain the band where eviction really
        // happens - the panel would be wrong about the one thing it exists to show.
        //
        // Deliberately NOT fixed by normalizing worldDefinition.streamingConfig instead: that
        // member is what Save World writes, and resolving it would bake VK-1591's prefetchRadius
        // 0 sentinel into loadRadius on disk.
        const auto& config = streamer.getConfig();
        const float sectorSize = sectorManager.getConfig().sectorWorldSize;
        if (!(sectorSize > 0.0f))
            return snapshot;

        // The ticket's ring: unloadRadius + 2, so the band the streamer is about to evict is
        // visible rather than clipped off the edge of the panel.
        //
        // Sanitize in FLOAT first. unloadRadius is only slider-bounded in the editor - a script or
        // plugin SetStreamingConfigCommand can put anything in it, and casting a NaN or an
        // out-of-int32 float is UB rather than a large number. Likewise cap maxRadius below the
        // half-window, or the clamp bounds below invert (std::clamp is UB when lo > hi).
        constexpr int32_t kMaxOverlayRadius = 64;
        const float safeUnload = std::isfinite(config.unloadRadius)
            ? std::clamp(config.unloadRadius, 0.0f, static_cast<float>(kMaxOverlayRadius))
            : 0.0f;
        const int32_t wanted = static_cast<int32_t>(std::ceil(safeUnload)) + 2;
        const int32_t cap = std::clamp(maxRadius, 1, kMaxOverlayRadius);
        const int32_t radius = std::clamp(wanted, 1, cap);

        // lastStreamingOrigin is only written on frames the streaming gate was open. With the gate
        // shut (edit mode without editModeStreaming) the streamer never ran, so fall back to the
        // editor viewport camera - the same position the Sector Grid tab centres on.
        const glm::vec3 origin = (isPlayMode || config.editModeStreaming) ? lastStreamingOrigin
                                                                          : cachedCameraPos;

        snapshot.valid = true;
        // Clamp into the addressable sector window before the loop below adds +/- radius to it:
        // a NaN or wildly out-of-range camera position would otherwise overflow the loop bounds.
        {
            const world::SectorCoord raw = sectorManager.worldPositionToSectorCoord(origin);
            snapshot.center = world::SectorCoord(
                std::clamp(raw.x, world::kMinSectorCoord + radius, world::kMaxSectorCoord - radius),
                std::clamp(raw.z, world::kMinSectorCoord + radius, world::kMaxSectorCoord - radius));
        }
        snapshot.radius = radius;
        snapshot.radiusClamped = wanted > cap;
        snapshot.sectorWorldSize = sectorSize;
        snapshot.loadRadius = config.loadRadius;
        snapshot.prefetchRadius = world::effectivePrefetchRadius(config);
        snapshot.unloadRadius = config.unloadRadius;
        snapshot.overrideActive = streamingConfigOverride.has_value();
        snapshot.paused = streamer.isPaused();
        snapshot.burstFramesRemaining = streamer.getBurstFramesRemaining();

        // clear() keeps capacity, so after the first fill this whole loop allocates nothing.
        overlayCells.clear();
        overlaySources.clear();

        const auto& tiers = worldDefinition.hlodConfig.tiers;
        const bool hlodEnabled = worldDefinition.hlodConfig.enabled && !tiers.empty();

        // z descending so the first row drawn is the northernmost, matching the Sector Grid tab.
        for (int32_t z = snapshot.center.z + radius; z >= snapshot.center.z - radius; --z)
        {
            for (int32_t x = snapshot.center.x - radius; x <= snapshot.center.x + radius; ++x)
            {
                ::events::world::StreamingOverlayCell cell;
                cell.coord = world::SectorCoord(x, z);

                if (const world::WorldSector* sector = sectorManager.getSector(cell.coord))
                {
                    cell.exists = true;
                    cell.state = sector->state;
                    cell.dirty = sector->dirty;
                }

                if (hlodEnabled)
                {
                    // At most three hash lookups per cell. Deliberately not
                    // world::cellsContainingSector, which returns a freshly allocated vector.
                    for (const auto& tier : tiers)
                    {
                        const auto* proxy =
                            hlodProxyManager.getProxy(world::sectorToCell(cell.coord, tier));
                        if (proxy && (proxy->state == world::HLODProxyState::Loaded ||
                                      proxy->state == world::HLODProxyState::FadingIn))
                        {
                            cell.hlodVisible = true;
                            cell.hlodTier = tier.tier;
                            break;
                        }
                    }
                }

                overlayCells.push_back(cell);
            }
        }

        // Source 0 is the camera the service contributes itself; it is not in streamingSources.
        {
            ::events::world::StreamingOverlaySource cameraSource;
            cameraSource.position = origin;
            cameraSource.isCamera = true;
            overlaySources.push_back(cameraSource);
        }
        {
            std::lock_guard lock(streamingSourcesMutex);
            for (const auto& [id, src] : streamingSources)
            {
                ::events::world::StreamingOverlaySource source;
                source.position = src.position;
                source.radiusMultiplier = src.radiusMultiplier;
                source.priority = src.priority;
                source.targetState = src.targetState;
                overlaySources.push_back(source);
            }
        }

        snapshot.cells = overlayCells.data();
        snapshot.cellCount = static_cast<uint32_t>(overlayCells.size());
        snapshot.sources = overlaySources.data();
        snapshot.sourceCount = static_cast<uint32_t>(overlaySources.size());
        return snapshot;
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
            [this](const ::events::world::IsWorldModeQuery&) -> bool
            {
                // Explicit -> bool: without it the deduced return type is std::atomic<bool>,
                // which is not copyable.
                return worldMode;
            });

        // The EFFECTIVE config - what the streamer is actually running, session override included.
        // VK-1595: the editor's persistent sliders must NOT seed from this, or an active override
        // would be copied into worldDefinition on the next drag. They use
        // GetPersistedStreamingConfigQuery below.
        dispatcher.registerQueryHandler<::events::world::GetWorldStreamingStatsQuery>(
            [this](const ::events::world::GetWorldStreamingStatsQuery&)
            {
                return streamer.getConfig();
            });

        // VK-1595: the world's own config, i.e. exactly what Save World will write.
        dispatcher.registerQueryHandler<::events::world::GetPersistedStreamingConfigQuery>(
            [this](const ::events::world::GetPersistedStreamingConfigQuery&)
            {
                return worldDefinition.streamingConfig;
            });

        // VK-1591: prefetch-ring residency for the streaming overlay. `bytes` is EXACT (the raw
        // .vfsector bytes held), unlike SectorMetadata::estimatedMemory, which is the on-disk
        // header figure.
        dispatcher.registerQueryHandler<::events::world::GetSectorPrefetchStatsQuery>(
            [this](const ::events::world::GetSectorPrefetchStatsQuery&)
            {
                ::events::world::SectorPrefetchStats stats;
                stats.bytes = prefetchedBytes;
                stats.byteCap = getEffectiveStreamingConfig().maxPrefetchBytes; // VK-1595
                stats.burstFramesRemaining = streamer.getBurstFramesRemaining(); // VK-1593
                sectorManager.forEachSector([&stats](const world::WorldSector& sector)
                {
                    if (sector.state == world::SectorState::Prefetched)
                        ++stats.prefetchedSectors;
                    else if (sector.state == world::SectorState::Prefetching)
                        ++stats.prefetchingSectors;
                });
                return stats;
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
                    // VK-1591: deliberately NOT extended to Prefetching/Prefetched. Callers of
                    // this query expect sectors whose entities exist or are about to.
                    if (sector.state == world::SectorState::Loaded ||
                        sector.state == world::SectorState::Loading)
                    {
                        result.push_back(sector.coord);
                    }
                });
                return result;
            });

        dispatcher.registerQueryHandler<::events::world::GetAllSectorCoordsQuery>(
            [this](const ::events::world::GetAllSectorCoordsQuery&)
            {
                std::vector<world::SectorCoord> result;
                sectorManager.forEachSector([&](const world::WorldSector& sector)
                {
                    result.push_back(sector.coord);
                });
                return result;
            });

        dispatcher.registerCommandHandler<::events::world::SetSectorDataLayerCommand>(
            [this](const ::events::world::SetSectorDataLayerCommand& cmd) -> bool
            {
                if (!worldMode || cmd.layerName.empty()) return false;
                auto* sector = sectorManager.getSector(cmd.coord);
                if (!sector) return false;
                if (!world::canMutateDataLayers(*sector)) return false;

                sector->dataLayers[cmd.layerName] = cmd.data;
                // Edit-mode only dirty: a dirty sector is never auto-unloaded by the streamer, so
                // dirtying in play mode would pin it forever.
                //
                // VK-1596: the old note here claimed play-mode writes "live in memory until an
                // editor-mode save". They do not. Leaving play runs sectorManager.clear() (see the
                // EditorModeChangedNotification handler below), which destroys every WorldSector
                // and every dataLayers map with it - so a play-mode write is lost at Stop, not
                // merely unsaved. The editor blocks layer editing in play mode for that reason.
                if (!isPlayMode)
                    sector->dirty = true;
                return true;
            });

        dispatcher.registerCommandHandler<::events::world::RemoveSectorDataLayerCommand>(
            [this](const ::events::world::RemoveSectorDataLayerCommand& cmd) -> bool
            {
                if (!worldMode) return false;
                auto* sector = sectorManager.getSector(cmd.coord);
                if (!sector) return false;
                if (!world::canMutateDataLayers(*sector)) return false;

                if (sector->dataLayers.erase(cmd.layerName) == 0)
                    return false;
                if (!isPlayMode)
                    sector->dirty = true;
                return true;
            });

        dispatcher.registerQueryHandler<::events::world::GetSectorDataLayerQuery>(
            [this](const ::events::world::GetSectorDataLayerQuery& q)
                -> std::optional<std::vector<uint8_t>>
            {
                const auto* sector = sectorManager.getSector(q.coord);
                if (!sector) return std::nullopt;

                auto it = sector->dataLayers.find(q.layerName);
                if (it == sector->dataLayers.end()) return std::nullopt;
                return it->second;
            });

        // VK-1596: everything the editor's Data Layers tab shows, in one poll. Built by value -
        // see the note on the query - and only over Loaded sectors.
        dispatcher.registerQueryHandler<::events::world::GetDataLayersSummaryQuery>(
            [this](const ::events::world::GetDataLayersSummaryQuery&)
            {
                world::DataLayerInventory inventory;
                if (worldMode)
                    world::summarizeDataLayers(sectorManager, inventory);
                return inventory;
            });

        dispatcher.registerQueryHandler<::events::world::GetSectorReadinessQuery>(
            [this](const ::events::world::GetSectorReadinessQuery& q)
                -> ::events::world::SectorReadiness
            {
                ::events::world::SectorReadiness readiness;
                const auto* sector = sectorManager.getSector(q.coord);
                if (!sector)
                    return readiness;

                readiness.state = sector->state;
                readiness.fileLoadPending = pendingAsyncLoads.contains(q.coord);
                readiness.entitySpawnsPending = entityLoader.hasPendingLoadsForSector(q.coord);
                readiness.entityCount = static_cast<uint32_t>(sector->entityUUIDs.size());
                return readiness;
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

        // The PERSISTED config - this is the one Save World writes.
        // VK-1595: it no longer borrows the streamer as its validator, because with a session
        // override installed the streamer is running something else entirely. It normalizes the
        // value directly and then re-pushes whichever config is effective.
        dispatcher.registerCommandHandler<::events::world::SetStreamingConfigCommand>(
            [this](const ::events::world::SetStreamingConfigCommand& cmd)
            {
                if (!worldMode) return;
                worldDefinition.streamingConfig = cmd.config;
                world::normalizeStreamingConfig(worldDefinition.streamingConfig);
                applyEffectiveStreamingConfig(); // an active override still wins
            });

        // ---- VK-1595: session override (never persisted) ----
        dispatcher.registerCommandHandler<::events::world::SetStreamingConfigOverrideCommand>(
            [this](const ::events::world::SetStreamingConfigOverrideCommand& cmd)
            {
                if (!worldMode) return;
                world::SectorStreamingConfig normalized = cmd.config;
                world::normalizeStreamingConfig(normalized);
                streamingConfigOverride = normalized;
                applyEffectiveStreamingConfig();
            });

        dispatcher.registerCommandHandler<::events::world::ClearStreamingConfigOverrideCommand>(
            [this](const ::events::world::ClearStreamingConfigOverrideCommand&)
            {
                if (!streamingConfigOverride.has_value()) return;
                streamingConfigOverride.reset();
                // Snap the streamers back onto the world's own config in the same frame, so
                // "clear" is observable immediately rather than at the next config edit.
                applyEffectiveStreamingConfig();
            });

        dispatcher.registerQueryHandler<::events::world::GetStreamingConfigOverrideQuery>(
            [this](const ::events::world::GetStreamingConfigOverrideQuery&)
            {
                return streamingConfigOverride;
            });

        // ---- VK-1595: freeze / single-step ----
        dispatcher.registerCommandHandler<::events::world::SetStreamingPausedCommand>(
            [this](const ::events::world::SetStreamingPausedCommand& cmd)
            {
                streamer.setPaused(cmd.paused);
            });

        dispatcher.registerCommandHandler<::events::world::StepStreamingFrameCommand>(
            [this](const ::events::world::StepStreamingFrameCommand&)
            {
                streamer.requestStep();
            });

        dispatcher.registerQueryHandler<::events::world::GetStreamingPausedQuery>(
            [this](const ::events::world::GetStreamingPausedQuery&)
            {
                return streamer.isPaused();
            });

        // ---- VK-1595: in-viewport overlay ----
        dispatcher.registerCommandHandler<::events::world::SetStreamingOverlayVisibleCommand>(
            [this](const ::events::world::SetStreamingOverlayVisibleCommand& cmd)
            {
                streamingOverlayVisible = cmd.visible;
            });

        dispatcher.registerQueryHandler<::events::world::GetStreamingOverlayVisibleQuery>(
            [this](const ::events::world::GetStreamingOverlayVisibleQuery&)
            {
                return streamingOverlayVisible;
            });

        dispatcher.registerQueryHandler<::events::world::GetStreamingOverlaySnapshotQuery>(
            [this](const ::events::world::GetStreamingOverlaySnapshotQuery& query)
            {
                return buildOverlaySnapshot(query.maxRadius);
            });

        dispatcher.registerQueryHandler<::events::world::GetAlwaysLoadedMigrationCountQuery>(
            [this](const ::events::world::GetAlwaysLoadedMigrationCountQuery&)
            {
                return alwaysLoadedMigrationCount;
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
                // VK-1589: no world, no source. Registering outside world mode used to hand out a
                // live id that no cleanup path could reach (both the mode-change clear and the
                // entity-delete sweep are worldMode-gated), and it contradicted the documented
                // script contract "returns 0 if no sector world is active".
                if (!worldMode)
                    return 0;

                std::lock_guard lock(streamingSourcesMutex);
                uint32_t id = nextStreamingSourceId++;
                world::StreamingSource source;
                source.position = cmd.position;
                source.radiusMultiplier = cmd.radiusMultiplier;
                source.priority = cmd.priority;
                source.targetState = cmd.targetState; // VK-1591
                source.id = id;
                streamingSources[id] = source;
                if (cmd.ownerEntityUUID != 0)
                    streamingSourceOwners[id] = cmd.ownerEntityUUID;
                return id;
            });

        dispatcher.registerCommandHandler<::events::world::UnregisterStreamingSourceCommand>(
            [this](const ::events::world::UnregisterStreamingSourceCommand& cmd)
            {
                std::lock_guard lock(streamingSourcesMutex);
                streamingSources.erase(cmd.sourceId);
                streamingSourceOwners.erase(cmd.sourceId);
            });

        dispatcher.registerCommandHandler<::events::world::UpdateStreamingSourcePositionCommand>(
            [this](const ::events::world::UpdateStreamingSourcePositionCommand& cmd)
            {
                std::lock_guard lock(streamingSourcesMutex);
                auto it = streamingSources.find(cmd.sourceId);
                if (it != streamingSources.end())
                    it->second.position = cmd.position;
            });

        dispatcher.registerQueryHandler<::events::world::IsStreamingSourceValidQuery>(
            [this](const ::events::world::IsStreamingSourceValidQuery& query) -> bool
            {
                std::lock_guard lock(streamingSourcesMutex);
                return streamingSources.contains(query.sourceId);
            });

        // HLOD commands/queries
        dispatcher.registerCommandHandler<::events::world::hlod::GenerateHLODCommand>(
            [this](const ::events::world::hlod::GenerateHLODCommand& cmd) -> bool
            {
                return generateSectorHLOD(cmd.coord, cmd.tier);
            });

        dispatcher.registerCommandHandler<::events::world::hlod::SetHLODConfigCommand>(
            [this](const ::events::world::hlod::SetHLODConfigCommand& cmd)
            {
                worldDefinition.hlodConfig = cmd.config;
                applyEffectiveStreamingConfig(); // VK-1595: honour an active session override
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

        // VK-1594: bakes every configured tier, asynchronously. Returns whether the bake STARTED.
        dispatcher.registerCommandHandler<::events::world::hlod::GenerateAllHLODCommand>(
            [this](const ::events::world::hlod::GenerateAllHLODCommand& cmd) -> bool
            {
                return beginHLODBake(cmd.missingOnly);
            });

        dispatcher.registerCommandHandler<::events::world::hlod::CancelHLODBakeCommand>(
            [this](const ::events::world::hlod::CancelHLODBakeCommand&)
            {
                hlodBaker.cancel();
            });

        dispatcher.registerQueryHandler<::events::world::hlod::GetHLODBakeProgressQuery>(
            [this](const ::events::world::hlod::GetHLODBakeProgressQuery&)
            {
                return hlodBaker.getProgress();
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

                    // VK-1594: a bake is an edit-mode authoring action. Entering play mode stops
                    // it rather than letting workers keep chewing through sector files while the
                    // game runs (and while savedWorldDefinition is swapped out underneath).
                    hlodBaker.cancel();

                    // VK-1589: start the session with no gameplay sources, symmetric with the
                    // Edit branch below. Anything registered in edit mode (a nav invoker, a
                    // leftover) must not pin sectors for the play session.
                    clearStreamingSources();

                    // Entering play mode — simulate runtime: unload all sectors so they
                    // stream in based on camera distance (like a fresh world load)
                    savedWorldDefinition = worldDefinition;
                    savedWorldPath = currentWorldPath;
                    // VK-1591: every sector is force-reset to Unloaded below, so any cached blob
                    // would be orphaned. The drain is a correctness fix beyond the prefetch ring:
                    // a Loading future still in flight would otherwise land in a later poll and
                    // spawn the edit-mode sector's entities into the play scene.
                    // VK-1592: must go through drainSectorLoads - a request still queued in the
                    // scheduler has no worker behind it, so a bare AsyncLoadQueue::drain() would
                    // block on a promise nobody will ever satisfy. HLOD proxies deliberately
                    // survive the mode change, so their in-flight reads are left alone.
                    drainSectorLoads();
                    clearPrefetchedBlobs();
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

                    // Sector states changed wholesale — make the streamer reseed its tracking
                    streamer.setEnabled(true);
                    // VK-1595: and drop the freeze with them. Pause promises "resume from exactly
                    // where you froze", but every sector was just force-reset to Unloaded above, so
                    // there is nothing left to resume into - a streamer still paused here would
                    // leave play mode staring at a completely empty world, with the only cure
                    // buried in the World Sectors window. Same reasoning as the world-close reset;
                    // the session config override is deliberately NOT dropped, it survives a wipe
                    // intact and is what the developer is mid-way through tuning.
                    streamer.setPaused(false);
                }
                else if (notif.currentMode == services::EditorMode::Edit)
                {
                    isPlayMode = false;
                    physicsSnapshots.clear();
                    animationSnapshots.clear();
                    vfxSnapshots.clear();
                    audioSnapshots.clear();
                    clearStreamingSources();

                    // Returning to edit mode — snapshot was restored, re-assign entities to sectors
                    // VK-1591: sectorManager.clear() destroys every WorldSector, so blobs keyed on
                    // their coords must go with them (see the loadWorld note).
                    drainSectorLoads(); // VK-1592: see the Play branch above
                    clearPrefetchedBlobs();
                    entityLoader.clear();
                    sectorManager.clear();
                    sectorManager.setConfig(savedWorldDefinition.sectorConfig);
                    worldDefinition = savedWorldDefinition;
                    currentWorldPath = savedWorldPath;

                    for (const auto& [coord, storedSectorPath] : worldDefinition.sectorFilePaths)
                    {
                        auto& sector = sectorManager.getOrCreateSector(coord);
                        sector.filePath = resolveProjectPath(storedSectorPath);
                        sector.state = world::SectorState::Unloaded;
                    }

                    auto& root = sceneGraph->GetRoot();
                    const auto& reassignConfig = sectorManager.getConfig();
                    for (auto& child : root.getChildren())
                    {
                        // VK-1597: this is also the recovery path for a policy flip attempted
                        // during play - the flag is honoured here even though the migration
                        // handler refused it mid-session.
                        const auto assignment = world::resolveEntitySectorAssignment(child, reassignConfig);
                        if (!assignment.isSpatial())
                            continue;

                        sectorManager.assignEntityToSector(child.getUUID().getValue(), assignment.coord);
                    }

                    sectorManager.forEachSector([](world::WorldSector& sector)
                    {
                        if (!sector.entityUUIDs.empty())
                        {
                            sector.state = world::SectorState::Loaded;
                        }
                    });

                    // VK-1595: worldDefinition was just rolled back to the play-entry snapshot, so
                    // re-push whichever config is now effective. The session override is
                    // deliberately NOT cleared - it belongs to the tuning session, not the play
                    // session - and it keeps winning here.
                    //
                    // This also closes a latent divergence that predates VK-1595: the restore
                    // above reverts worldDefinition.streamingConfig but nothing ever re-pushed it,
                    // so a config edited during play left the streamer running one value while the
                    // next Save World would have written another.
                    applyEffectiveStreamingConfig();

                    // Sector states changed wholesale — make the streamer reseed its tracking
                    streamer.setEnabled(true);
                    streamer.setPaused(false); // VK-1595: symmetric with the Play branch above
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
                // VK-1597: an always-loaded entity has no sector to migrate between, so the
                // isSpatial() gate subsumes the old type skip-list here. (hasEntitySector below
                // already covers it once the entity has been migrated out, but not in the window
                // before the flip is applied.)
                if (!world::resolveEntitySectorAssignment(sceneEntity, sectorManager.getConfig()).isSpatial())
                    return;

                uint64_t uuid = sceneEntity.getUUID().getValue();
                if (sectorManager.hasEntitySector(uuid))
                {
                    onTransformChanged(uuid, notif.newTransform.position);
                }
            });

        // VK-1597: the interactive half of the streaming-policy flip. A notification rather than a
        // command handler because EntityStateService owns the component - this service only owns
        // the sector membership that has to follow it.
        streamingPolicyChangedToken =
            dispatcher.subscribe<::events::scene::EntityStreamingPolicyChangedNotification>(
            [this](const ::events::scene::EntityStreamingPolicyChangedNotification& notif)
            {
                if (!worldMode) return;

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(notif.entity);
                if (!registry.valid(entity)) return;

                auto* uuidComp = registry.try_get<components::UUIDComponent>(entity);
                if (!uuidComp) return;

                onStreamingPolicyChanged(uuidComp->id.getValue(), notif.spatiallyLoaded);
            });

        // VK-1597: the scene file is what actually persists an always-loaded entity, so a
        // successful scene save - not Save World - is what clears the outstanding-migration
        // warning.
        sceneSavedToken = dispatcher.subscribe<::events::scene::SceneSavedNotification>(
            [this](const ::events::scene::SceneSavedNotification&)
            {
                alwaysLoadedMigrationCount = 0;
            });

        // Auto-unregister streaming sources whose owning entity is deleted
        entityDeletedToken = dispatcher.subscribe<::events::scene::EntityDeletedNotification>(
            [this](const ::events::scene::EntityDeletedNotification& notif)
            {
                if (!worldMode) return;

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(notif.entity);
                if (!registry.valid(entity)) return;

                auto* uuidComp = registry.try_get<components::UUIDComponent>(entity);
                if (!uuidComp) return;
                uint64_t uuid = uuidComp->id.getValue();

                std::lock_guard lock(streamingSourcesMutex);
                if (streamingSourceOwners.empty()) return;

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
                    if (resource::VirtualFileSystem::instance().exists(worldPath.string()))
                    {
                        std::string wp = worldPath.string();
                        loadWorld(wp);

                        // Tag root so future saves include it — stored project-relative so the
                        // scene still names this world once it ships inside a .vfpak
                        root.addOrReplaceComponent<components::WorldSectorComponent>().worldFilePath =
                            toProjectRelativePath(wp);
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

                // VK-1597: HierarchyService::createEntity publishes this on a BARE entity, before
                // any component is deserialized onto it, so a prefab instance that is about to
                // receive StreamingPolicyComponent still lands in a sector here. That is what
                // reconcileAlwaysLoadedEntities() at Save World exists to undo.
                const auto assignment = world::resolveEntitySectorAssignment(sceneEntity, sectorManager.getConfig());
                if (!assignment.isSpatial())
                    return;

                uint64_t uuid = sceneEntity.getUUID().getValue();
                if (!sectorManager.hasEntitySector(uuid))
                {
                    sectorManager.assignEntityToSector(uuid, assignment.coord);
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

                    // VK-1591: this is the only teardown path a shipped Runtime takes. Without the
                    // drain, an in-flight read outlives the sector map it was keyed against.
                    // VK-1592: drainSectorLoads cancels requests the scheduler has not dispatched
                    // (a bare drain would hang); the world is going away, so HLOD reads go too.
                    drainSectorLoads();
                    drainHlodLoads();
                    clearPrefetchedBlobs();
                    entityLoader.clear();
                    sectorManager.clear();
                    worldDefinition = {};
                    // VK-1595: the world is going away, and with it the session override it was
                    // tuned against. A streamer left paused across the change would read as
                    // "streaming is broken" in the next world.
                    resetStreamingSessionState();
                    streamer.setEnabled(false);

                    // VK-1589: must happen BEFORE worldMode goes false. Both the mode-change
                    // clear and the entity-delete auto-unregister are worldMode-gated, so any
                    // source still registered here becomes permanently unreachable and would
                    // pin sectors in the next world. This is also the only cleanup path a
                    // shipped Runtime has - it never publishes EditorModeChangedNotification.
                    clearStreamingSources();

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

    bool WorldSectorServiceImpl::beginHLODBake(bool missingOnly)
    {
        // The worldMode guard is what keeps a bake out of play mode; do not weaken it.
        if (!worldMode) return false;
        if (hlodBaker.isRunning()) return false;
        if (worldDefinition.hlodConfig.tiers.empty()) return false;

        HLODWorldBaker::BakeRequest request;
        request.workingDirectory = hlodWorkingDirectory();
        request.sectorConfig = worldDefinition.sectorConfig;

        // Fine tier first, so the near ring becomes correct before the expensive far one, and so
        // the baker's per-tier concurrency cap narrows monotonically as cells get bigger.
        auto tiers = worldDefinition.hlodConfig.tiers;
        std::sort(tiers.begin(), tiers.end(),
                  [](const world::HLODTierConfig& a, const world::HLODTierConfig& b)
                  { return world::effectiveCellSize(a) < world::effectiveCellSize(b); });

        for (const auto& tier : tiers)
        {
            for (auto& plan : world::planCellsForTier(worldDefinition.sectorFilePaths, tier))
            {
                if (missingOnly && !resolveHLODCellPath(plan.cell, tier).empty())
                    continue;

                HLODWorldBaker::CellJob job;
                job.cell = plan.cell;
                job.tierConfig = tier;
                job.firstMemberCoord = plan.memberCoords.front();
                job.outputPath = world::hlodOutputPathForCell(plan.cell, currentWorldPath,
                                                              plan.memberSectorFiles.front());
                job.memberSectorFiles = std::move(plan.memberSectorFiles);
                request.jobs.push_back(std::move(job));
            }
        }

        if (request.jobs.empty())
        {
            vfLogInfo("HLOD bake: nothing to do");
            return false;
        }

        return hlodBaker.begin(std::move(request));
    }

    const world::HLODTierConfig* WorldSectorServiceImpl::findHLODTier(uint8_t tier) const
    {
        for (const auto& t : worldDefinition.hlodConfig.tiers)
        {
            if (t.tier == tier)
                return &t;
        }
        return nullptr;
    }

    std::string WorldSectorServiceImpl::resolveHLODCellPath(
        const world::HLODCellCoord& cell, const world::HLODTierConfig& tierConfig) const
    {
        if (auto it = worldDefinition.hlodCells.find(cell); it != worldDefinition.hlodCells.end())
            return it->second;

        // Fallback for worlds baked before the hlodCells inventory existed. Only tier 0 has a
        // filename convention that WorldSectorPersistenceOps can rediscover on load, so tiers 1+
        // simply have no bake until the world is re-baked.
        if (cell.tier == 0)
        {
            const world::SectorCoord origin = world::cellOriginSector(cell, tierConfig);
            if (const auto* sector = sectorManager.getSector(origin))
                return sector->hlodFilePath;
        }

        return {};
    }

    std::string WorldSectorServiceImpl::hlodWorkingDirectory() const
    {
        if (currentWorldPath.empty())
            return ".";

        const auto slash = currentWorldPath.find_last_of("/\\");
        return slash == std::string::npos ? "." : currentWorldPath.substr(0, slash);
    }

    bool WorldSectorServiceImpl::bakeHLODCell(const world::HLODCellCoord& cell)
    {
        if (!worldMode) return false;

        const auto* tierConfig = findHLODTier(cell.tier);
        if (!tierConfig) return false;

        // Re-plan from the live sector table rather than trusting a stale member list: a sector
        // may have been added or deleted since the cell was queued.
        auto plans = world::planCellsForTier(worldDefinition.sectorFilePaths, *tierConfig);
        const auto plan = std::find_if(plans.begin(), plans.end(),
                                       [&](const world::HLODCellPlan& p) { return p.cell == cell; });
        if (plan == plans.end() || plan->memberSectorFiles.empty())
            return false;

        const std::string outputPath = world::hlodOutputPathForCell(
            cell, currentWorldPath, plan->memberSectorFiles.front());

        world::HLODGenerator generator;
        const std::string workingDir = hlodWorkingDirectory();

        bool result = false;
        if (cell.tier == 0)
        {
            result = generator.generateForSector(plan->memberCoords.front(),
                                                 plan->memberSectorFiles.front(),
                                                 workingDir, *tierConfig, outputPath);
        }
        else
        {
            result = generator.generateForCell(cell, plan->memberSectorFiles, workingDir,
                                               *tierConfig, worldDefinition.sectorConfig,
                                               outputPath);
        }

        if (result)
            recordHLODBake(cell, outputPath);

        return result;
    }

    void WorldSectorServiceImpl::recordHLODBake(const world::HLODCellCoord& cell,
                                                const std::string& outputPath)
    {
        worldDefinition.hlodCells[cell] = outputPath;

        // Tier 0 also keeps WorldSector::hlodFilePath current: it is what the load-time filename
        // probe and IsHLODGeneratedQuery both read, and what old worlds fall back to.
        if (cell.tier == 0)
        {
            const auto* tierConfig = findHLODTier(0);
            if (tierConfig)
            {
                const world::SectorCoord origin = world::cellOriginSector(cell, *tierConfig);
                if (auto* sector = sectorManager.getSector(origin))
                    sector->hlodFilePath = outputPath;
            }
        }

        // A fresh bake supersedes whatever the streamer thinks it has for this cell.
        hlodStreamer.forgetProxy(cell);
    }

    bool WorldSectorServiceImpl::generateSectorHLOD(const world::SectorCoord& coord, uint8_t tier)
    {
        const auto* tierConfig = findHLODTier(tier);
        if (!tierConfig) return false;

        return bakeHLODCell(world::sectorToCell(coord, *tierConfig));
    }

    void WorldSectorServiceImpl::processHLODRegenQueue()
    {
        // One re-bake per frame, edit mode only, and only while sector streaming is idle so
        // re-bakes never compete with loads. This path is still SYNCHRONOUS on the main thread:
        // it is a single stale cell, not a whole-world bake, and running it through the async
        // baker would fight the explicit Generate All the user may have started.
        if (hlodRegenQueue.empty() || isPlayMode || !worldDefinition.hlodConfig.enabled)
            return;
        if (!pendingAsyncLoads.empty())
            return;

        // VK-1594: never re-bake behind the async baker's back - it holds an immutable snapshot
        // of the sector list and would race this write to worldDefinition.hlodCells.
        if (hlodBaker.isRunning())
            return;

        const auto cell = hlodRegenQueue.front();
        hlodRegenQueue.pop_front();

        // Already re-baked (manually, or by an earlier queue entry covering the same cell)
        if (worldDefinition.hlodCells.count(cell) > 0)
            return;

        if (bakeHLODCell(cell))
            vfLogInfo("HLOD re-baked for cell [{},{},T{}]", cell.x, cell.z, cell.tier);
        else
            vfLogWarning("HLOD re-bake failed for cell [{},{},T{}]", cell.x, cell.z, cell.tier);
    }

    void WorldSectorServiceImpl::invalidateHLODForSector(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector)
            return;

        // VK-1594: cascade into every tier that contains this sector, not just its own tier-0
        // bake. A 4x4 tier-2 cell is stale the moment any one of its 16 sectors changes.
        bool invalidatedAny = false;
        for (const auto& tier : worldDefinition.hlodConfig.tiers)
        {
            const world::HLODCellCoord cell = world::sectorToCell(coord, tier);
            const std::string stalePath = resolveHLODCellPath(cell, tier);

            if (!stalePath.empty())
            {
                // Delete the stale bake so loadWorld's disk probe doesn't resurrect it
                std::error_code ec;
                std::filesystem::remove(stalePath, ec);
                invalidatedAny = true;
            }

            worldDefinition.hlodCells.erase(cell);
            if (tier.tier == 0)
                sector->hlodFilePath.clear();

            // Drop any loaded proxy covering this sector (real or future geometry replaces it;
            // the streamer re-emits a load once a new bake exists).
            // VK-1592: the read now lives in the scheduler, not inside HLODProxyManager, so
            // unloadProxy no longer discards it implicitly - a load still in flight would
            // rebuild a proxy from the bake we just deleted.
            cancelHlodRequest(cell);
            releaseHLODProxy(cell);
            hlodStreamer.forgetProxy(cell);

            // Queue an automatic re-bake (drained when streaming is idle, edit mode)
            if (worldDefinition.hlodConfig.enabled &&
                std::find(hlodRegenQueue.begin(), hlodRegenQueue.end(), cell) == hlodRegenQueue.end())
            {
                hlodRegenQueue.push_back(cell);
            }
        }

        if (!invalidatedAny)
            return;

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

    void WorldSectorServiceImpl::clearStreamingSources()
    {
        // VK-1593: velocity history is derived from these sources, so it has to die with them -
        // otherwise the first frame after Edit<->Play or a scene clear derives a velocity from a
        // position that belonged to a different world. The streamer's own motion map has to go
        // with it for the same reason, and because nextStreamingSourceId below rewinds to 1: a
        // newly registered source would otherwise inherit the last position of the PREVIOUS
        // world's id-1 source and read as a teleport on its very first frame.
        lastSourcePositionsForVelocity.clear();
        sourceVelocityScratch.clear();
        streamer.resetMotionTracking();

        std::lock_guard lock(streamingSourcesMutex);
        streamingSources.clear();
        streamingSourceOwners.clear();
        nextStreamingSourceId = 1;
    }

} // namespace services
