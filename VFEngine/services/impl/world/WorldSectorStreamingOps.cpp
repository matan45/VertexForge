#include "WorldSectorServiceImpl.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "world/WorldSectorSerialization.hpp"
#include "serialization/SceneSerialization.hpp"
#include "resource/ResourceLoadScheduler.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/render/DebugDrawEvents.hpp"
#include "../../events/render/LightStreamingEvents.hpp"
#include "../../events/render/ObjectStreamingEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "print/Log.hpp"
#include "threading/JobSystem.hpp"
#include <atomic>
#include <nlohmann/json.hpp>

namespace
{
    static constexpr uint32_t kMaxConcurrentSectorLoads = 4;

    // VK-1590: WorldSector::entityUUIDs is the PARSE-TIME ROOT list. It overstates reality
    // (entities whose spawn threw, or that were deduped away, stay in it) and understates it
    // (nested children are absent — SectorEntityLoader only records payload roots). The
    // reference resolver keys on "what actually exists", so filter and expand before feeding it.
    void collectSubtreeUUIDs(entt::registry& registry, entt::entity entity,
                             std::vector<uint64_t>& out)
    {
        if (entity == entt::null || !registry.valid(entity))
            return;

        if (const auto* uuidComp = registry.try_get<components::UUIDComponent>(entity))
        {
            out.push_back(uuidComp->id.getValue());
        }

        if (const auto* childrenComp = registry.try_get<components::ChildrenComponent>(entity))
        {
            for (auto child : childrenComp->children)
            {
                collectSubtreeUUIDs(registry, child, out);
            }
        }
    }

    std::vector<uint64_t> expandLiveUUIDs(const std::vector<uint64_t>& rootUUIDs)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<uint64_t> live;
        live.reserve(rootUUIDs.size());
        for (uint64_t uuid : rootUUIDs)
        {
            collectSubtreeUUIDs(registry, scene::EntityRegistry::findByUUID(uuid), live);
        }
        return live;
    }
}

namespace services
{
    void WorldSectorServiceImpl::update()
    {
#ifdef DEBUG
        // VK-1588: this function is main-thread-only. It mutates the ResourceLoadScheduler
        // singleton, runs a synchronous EventDispatcher::query, reads/writes scene::EntityRegistry,
        // spawns and destroys entities through SectorEntityLoader::update, and edits the scene
        // graph via HLOD proxy load/unload. Both hosts register the "WorldSector" frame task with
        // mainThread=true so it runs as an enki::LambdaPinnedTask on thread 0; this catches a
        // future caller that forgets. Checked before the worldMode guard so the editor confirms it
        // even before a world is opened. Logged once per outcome - a per-frame line would flood the
        // console and the ImGui log buffer.
        {
            static std::atomic<bool> confirmedOnMain{false};
            static std::atomic<bool> offThreadReported{false};
            if (threading::JobSystem::instance().isMainThread())
            {
                if (!confirmedOnMain.exchange(true, std::memory_order_relaxed))
                    vfLogInfo("[WorldSector] update() confirmed on the main thread (enkiTS thread 0)");
            }
            else if (!offThreadReported.exchange(true, std::memory_order_relaxed))
            {
                vfLogError("[WorldSector] update() ran OFF the main thread. The 'WorldSector' frame "
                           "task must be registered with mainThread=true - entity spawn/destroy, "
                           "EntityRegistry access and scene-graph edits here are not thread-safe.");
            }
        }
#endif

        if (!worldMode)
            return;

        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastUpdateTime).count();
        lastUpdateTime = now;

        // Poll completed async sector loads (works in both edit and play mode)
        pollAsyncSectorLoads();

        // Stream sectors during play mode (primary camera distance), and optionally in
        // edit mode off the editor viewport camera when editModeStreaming is enabled.
        // Dirty (unsaved) sectors are never auto-unloaded by the streamer, so edit-mode
        // streaming cannot drop unsaved work.
        if (isPlayMode || worldDefinition.streamingConfig.editModeStreaming)
        {
            // Build streaming sources: camera is always source[0].
            // The editor camera isn't an ECS entity — use the cached viewport position.
            std::vector<world::StreamingSource> sources;
            {
                world::StreamingSource cameraSrc;
                cameraSrc.position = isPlayMode ? getPrimaryCameraPosition() : cachedCameraPos;
                cameraSrc.radiusMultiplier = 1.0f;
                cameraSrc.priority = 0;
                cameraSrc.id = 0;
                sources.push_back(cameraSrc);
            }
            // VK-1589: snapshot under the lock, then release it. streamer.update() below is the
            // long call and takes nothing, so holding across it would serialise script
            // registration against the whole streaming pass for no benefit.
            {
                std::lock_guard lock(streamingSourcesMutex);
                sources.reserve(sources.size() + streamingSources.size());
                for (const auto& [id, src] : streamingSources)
                    sources.push_back(src);
            }

            // Update resource load scheduler with current camera position for priority re-computation
            resource::ResourceLoadScheduler::instance().update(sources[0].position);

            streamer.update(sources, sectorManager, streamingActions);

            // Edit-mode rail: never auto-unload the sector holding the selected entity —
            // panels and gizmos hold live references to it
            std::optional<world::SectorCoord> selectedCoord;
            if (!isPlayMode)
            {
                auto selected = ::events::EventDispatcher::instance().query(
                    ::events::scene::GetSelectedEntityQuery{});
                if (selected.has_value())
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto entity = internal::fromHandle(*selected);
                    if (registry.valid(entity))
                    {
                        if (auto* uuidComp = registry.try_get<components::UUIDComponent>(entity))
                        {
                            uint64_t uuid = uuidComp->id.getValue();
                            if (sectorManager.hasEntitySector(uuid))
                                selectedCoord = sectorManager.getEntitySector(uuid);
                        }
                    }
                }
            }

            for (const auto& action : streamingActions)
            {
                if (action.isLoad)
                {
                    handleSectorLoad(action.coord);
                }
                else
                {
                    if (selectedCoord.has_value() && action.coord == *selectedCoord)
                    {
                        // The streamer already dropped this coord from its tracking;
                        // force a reseed so it retries once the selection moves on
                        streamer.setEnabled(true);
                        continue;
                    }
                    handleSectorUnload(action.coord);
                }
            }

            // HLOD proxy streaming (beyond sector unload radius)
            if (worldDefinition.hlodConfig.enabled)
            {
                hlodActions.clear();
                hlodStreamer.update(sources, sectorManager,
                                   worldDefinition.sectorConfig, hlodActions);

                for (const auto& action : hlodActions)
                {
                    if (action.isLoad)
                    {
                        // Resolve HLOD file path from the cell's sectors
                        auto tier = action.cellCoord.tier;
                        int32_t cs = (tier < worldDefinition.hlodConfig.tiers.size())
                            ? worldDefinition.hlodConfig.tiers[tier].cellSize : 1;
                        int32_t baseX = action.cellCoord.x * cs;
                        int32_t baseZ = action.cellCoord.z * cs;
                        world::SectorCoord baseSector(baseX, baseZ);

                        const auto* sector = sectorManager.getSector(baseSector);
                        if (sector && !sector->hlodFilePath.empty())
                        {
                            hlodProxyManager.loadProxy(action.cellCoord, sector->hlodFilePath);
                        }
                    }
                    else
                    {
                        hlodProxyManager.unloadProxy(action.cellCoord, *sceneGraph);
                    }
                }

                hlodProxyManager.update(*sceneGraph, deltaTime);
            }
        }

        drawDebugSectors();

        processHLODRegenQueue();

        entityLoader.update(*sceneGraph, worldDefinition.streamingConfig.maxEntitiesPerFrame);

        // VK-1590: entities spawned this frame whose reference target was ALREADY resident.
        // Batched by the PostLoad hook so the whole spawn budget costs one resolver pass.
        if (!refTargetProbeQueue.empty())
        {
            referenceResolver.onSectorLoaded(refTargetProbeQueue);
            refTargetProbeQueue.clear();
        }

        bool anySectorBecameLoaded = false;

        // Transition sectors from Loading to Loaded once all their entities are processed
        sectorManager.forEachSector([&](world::WorldSector& sector)
        {
            if (sector.state == world::SectorState::Loading &&
                !entityLoader.hasPendingLoadsForSector(sector.coord))
            {
                sector.state = world::SectorState::Loaded;
                anySectorBecameLoaded = true;

                // Register sector lights for streaming
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    std::vector<uint32_t> lightEntityIds;
                    for (uint64_t uuid : sector.entityUUIDs)
                    {
                        auto ent = scene::EntityRegistry::findByUUID(uuid);
                        if (ent != entt::null)
                        {
                            if (registry.any_of<components::PointLightComponent,
                                                components::SpotLightComponent>(ent))
                            {
                                lightEntityIds.push_back(static_cast<uint32_t>(ent));
                            }
                        }
                    }
                    if (!lightEntityIds.empty())
                    {
                        events::render::lightstreaming::RegisterSectorLightsCommand cmd;
                        cmd.sectorId = world::sectorCoordToId(sector.coord);
                        cmd.lightEntityIds = std::move(lightEntityIds);
                        ::events::EventDispatcher::instance().execute(cmd);
                    }
                }

                // Register sector mesh objects for GPU streaming (including sub-entities)
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    std::vector<std::pair<uint64_t, entt::entity>> meshEntities;

                    auto collectMeshEntities = [&](auto&& self, entt::entity ent) -> void
                    {
                        if (ent == entt::null || !registry.valid(ent)) return;

                        // VK-1579: only entity-backed meshes register for per-entity GPU object
                        // streaming (one non-instanced GPUObjectData per submesh). Foliage is
                        // packed FoliageInstance data on TerrainTile — entity-free by design — and
                        // renders via the instanced collector (FramePreparationSystem::collectFoliage),
                        // riding terrain-tile streaming. It must NEVER be materialized as entities
                        // here, or it would emit one non-instanced object per plant and exhaust
                        // MAX_GPU_OBJECTS (the exact catastrophe the packed store avoids).
                        if (registry.any_of<components::MeshComponent>(ent))
                        {
                            auto* uuidComp = registry.try_get<components::UUIDComponent>(ent);
                            if (uuidComp)
                            {
                                meshEntities.emplace_back(uuidComp->id.getValue(), ent);
                            }
                        }

                        auto* childrenComp = registry.try_get<components::ChildrenComponent>(ent);
                        if (childrenComp)
                        {
                            for (auto child : childrenComp->children)
                            {
                                self(self, child);
                            }
                        }
                    };

                    for (uint64_t uuid : sector.entityUUIDs)
                    {
                        auto ent = scene::EntityRegistry::findByUUID(uuid);
                        if (ent != entt::null)
                        {
                            collectMeshEntities(collectMeshEntities, ent);
                        }
                    }

                    if (!meshEntities.empty())
                    {
                        events::render::objectstreaming::RegisterSectorObjectsCommand cmd;
                        cmd.sectorId = world::sectorCoordToId(sector.coord);
                        cmd.entities = std::move(meshEntities);
                        ::events::EventDispatcher::instance().execute(cmd);
                    }
                }

                // Clear any unconsumed snapshots for this sector's entities
                for (uint64_t uuid : sector.entityUUIDs)
                {
                    physicsSnapshots.erase(uuid);
                    animationSnapshots.erase(uuid);
                    vfxSnapshots.erase(uuid);
                    audioSnapshots.erase(uuid);
                }

                ::events::world::SectorLoadedNotification notif;
                notif.coord = sector.coord;
                notif.entityCount = static_cast<uint32_t>(sector.entityUUIDs.size());
                ::events::EventDispatcher::instance().publish(notif);

                // VK-1590: feed the resolver what is actually in the registry, expanded through
                // each payload's nested children — not the parse-time root list.
                referenceResolver.onSectorLoaded(expandLiveUUIDs(sector.entityUUIDs));
            }
        });

        // VK-1590: single drain point, covering both the already-resident probe above and the
        // sector-completion promotions. Main thread, after every spawn this frame has landed.
        world::SectorRefFieldRegistry::applyResolvedReferences(
            scene::EntityRegistry::getRegistry(), referenceResolver);

        // VK-1590: streamed entities never went through a scene/prefab load, so nothing ever
        // resolved their render-texture source names (camera / RTT entity / material slot
        // bindings) — they were stuck at entt::null for the session. The sweep is idempotent
        // (it only fills handles that are still null); gate it so it costs at most one pass per
        // frame, and only in frames where a sector actually completed.
        if (anySectorBecameLoaded)
        {
            serialization::SceneSerialization::resolveRenderTextureSourceNames();
        }
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
            return false;

        handleSectorUnload(coord);
        return true;
    }

    void WorldSectorServiceImpl::handleSectorLoad(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector || sector->filePath.empty())
            return;

        // Already have a pending async load for this sector
        if (pendingAsyncLoads.contains(coord))
            return;

        // Respect concurrency limit — streamer will re-emit next frame
        if (pendingAsyncLoads.size() >= kMaxConcurrentSectorLoads)
            return;

        // Pre-notify subsystems (e.g. navmesh) so they can begin loading tiles
        // before the sector transitions to Loading and entities begin spawning
        {
            float sectorSize = sectorManager.getConfig().sectorWorldSize;
            ::events::world::SectorAboutToLoadNotification preNotif;
            preNotif.coord = coord;
            preNotif.sectorConfig = sectorManager.getConfig();
            preNotif.boundsMin = glm::vec3(
                static_cast<float>(coord.x) * sectorSize, -1000.0f,
                static_cast<float>(coord.z) * sectorSize);
            preNotif.boundsMax = glm::vec3(
                static_cast<float>(coord.x + 1) * sectorSize, 1000.0f,
                static_cast<float>(coord.z + 1) * sectorSize);
            ::events::EventDispatcher::instance().publish(preNotif);
        }

        sector->state = world::SectorState::Loading;

        // Notify subsystems (e.g. terrain) that this sector is now active
        {
            ::events::world::SectorActivatedNotification notif;
            notif.coord = coord;
            notif.sectorConfig = sectorManager.getConfig();
            ::events::EventDispatcher::instance().publish(notif);
        }

        std::string filePath = sector->filePath;

        pendingAsyncLoads.launch(coord,
            std::async(std::launch::async, [filePath]() -> AsyncSectorLoadResult {
                AsyncSectorLoadResult result;
                result.success = world::WorldSectorSerialization::loadSector(
                    filePath, result.entityData, &result.dataLayers);
                return result;
            }));
    }

    void WorldSectorServiceImpl::pollAsyncSectorLoads()
    {
        pendingAsyncLoads.poll([this](const world::SectorCoord& coord,
                                      AsyncSectorLoadResult result)
        {
            auto* sector = sectorManager.getSector(coord);
            if (!sector)
                return;

            if (!result.success)
            {
                sector->state = world::SectorState::Unloaded;
                vfLogError("Async sector load failed for ({},{})", coord.x, coord.z);
                return;
            }

            finalizeSectorLoad(coord, result.entityData, result.dataLayers);
        });
    }

    void WorldSectorServiceImpl::finalizeSectorLoad(const world::SectorCoord& coord,
                                                     std::vector<nlohmann::json>& entityData,
                                                     world::SectorDataLayers& dataLayers)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector)
            return;

        // Merge file layers under any in-memory ones: layers written at runtime
        // (e.g. fog-of-war) survive unload on the sector struct and are newer
        // than what the file holds
        for (auto& [layerName, bytes] : dataLayers)
            sector->dataLayers.try_emplace(layerName, std::move(bytes));
        for (const auto& [layerName, bytes] : sector->dataLayers)
        {
            ::events::world::SectorDataLayerLoadedNotification notif;
            notif.coord = coord;
            notif.layerName = layerName;
            ::events::EventDispatcher::instance().publish(notif);
        }

        sector->entityUUIDs.clear();
        std::vector<std::pair<std::string, nlohmann::json>> entityNamesAndJson;
        entityNamesAndJson.reserve(entityData.size());
        for (auto& data : entityData)
        {
            if (data.contains("uuid") && data["uuid"].is_number_unsigned())
            {
                sector->entityUUIDs.push_back(data["uuid"].get<uint64_t>());
            }
            std::string name = data.value("name", "Unnamed");
            entityNamesAndJson.emplace_back(std::move(name), std::move(data));
        }

        entityLoader.queueSectorLoadFromData(coord, entityNamesAndJson);

        // State stays Loading until all entities are processed (checked in update())
        sector->dirty = false; // Just loaded from disk — nothing to save
    }

    void WorldSectorServiceImpl::rescanEntityReferences()
    {
        // VK-1590: entities already in the scene when world mode is entered never pass through
        // the sector spawn path, so the PostLoad hook never sees them. Sweep the whole registry
        // once so a main-scene entity referencing a streamed one heals the same way. Idempotent:
        // addPendingReference dedups and applyReference just rewrites the same handle.
        auto& registry = scene::EntityRegistry::getRegistry();

        std::vector<world::PendingReference> refs;
        world::SectorRefFieldRegistry::collectAllReferences(registry, refs);
        if (refs.empty())
            return;

        std::vector<uint64_t> liveTargets;
        liveTargets.reserve(refs.size());
        for (const auto& ref : refs)
        {
            referenceResolver.addPendingReference(ref.sourceUUID, ref.targetUUID, ref.type);
            if (scene::EntityRegistry::findByUUID(ref.targetUUID) != entt::null)
            {
                liveTargets.push_back(ref.targetUUID);
            }
        }

        if (!liveTargets.empty())
        {
            referenceResolver.onSectorLoaded(liveTargets);
        }
        world::SectorRefFieldRegistry::applyResolvedReferences(registry, referenceResolver);
    }

    void WorldSectorServiceImpl::handleSectorUnload(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector)
            return;

        // Cancel any in-flight async file I/O for this sector — the result is
        // discarded on the next poll (std::async has no cooperative cancellation)
        pendingAsyncLoads.cancel(coord);

        sector->state = world::SectorState::Unloading;

        // Notify subsystems (e.g. terrain) that this sector is now inactive
        {
            ::events::world::SectorDeactivatedNotification notif;
            notif.coord = coord;
            notif.sectorConfig = sectorManager.getConfig();
            ::events::EventDispatcher::instance().publish(notif);
        }

        // Unregister sector objects and lights before entities are destroyed
        {
            events::render::objectstreaming::UnregisterSectorObjectsCommand objCmd;
            objCmd.sectorId = world::sectorCoordToId(coord);
            ::events::EventDispatcher::instance().execute(objCmd);
        }
        {
            events::render::lightstreaming::UnregisterSectorLightsCommand cmd;
            cmd.sectorId = world::sectorCoordToId(coord);
            ::events::EventDispatcher::instance().execute(cmd);
        }

        // Cancel any pending entity loads for this sector (prevents recreating entities after unload)
        entityLoader.cancelPendingLoads(coord);

        // Separate static entities (to unload) from dynamic entities (to keep alive)
        std::vector<uint64_t> staticUUIDs;
        std::vector<uint64_t> dynamicUUIDs;

        for (uint64_t uuid : sector->entityUUIDs)
        {
            bool isDynamic = false;
            auto ent = scene::EntityRegistry::findByUUID(uuid);
            if (ent != entt::null)
            {
                scene::Entity sceneEntity(ent);
                if (sceneEntity.hasComponent<components::TransformComponent>())
                {
                    isDynamic = !sceneEntity.getComponent<components::TransformComponent>().isStatic;
                }
            }

            if (isDynamic)
                dynamicUUIDs.push_back(uuid);
            else
                staticUUIDs.push_back(uuid);
        }

        // VK-1590: expand through nested children — a reference may point at, or be held by, a
        // sub-entity that never appears in the root-keyed entityUUIDs list.
        const std::vector<uint64_t> unloadedSubtree = expandLiveUUIDs(staticUUIDs);
        referenceResolver.onSectorUnloaded(unloadedSubtree);   // targets leaving -> demote to pending
        referenceResolver.removeReferencesFrom(unloadedSubtree); // sources leaving -> drop entirely

        // Only unload static entities — dynamic entities persist in the scene.
        // NOTE: the ROOT list, deliberately. sceneGraph.removeEntity already takes the whole
        // subtree, so queueing the expanded set would try to destroy children twice.
        entityLoader.queueSectorUnload(coord, staticUUIDs);

        // Clear the sector's entity list, then re-add dynamic entities so they remain tracked
        sector->entityUUIDs = dynamicUUIDs;
        sector->state = world::SectorState::Unloaded;

        ::events::world::SectorUnloadedNotification notif;
        notif.coord = coord;
        notif.sectorConfig = sectorManager.getConfig();
        ::events::EventDispatcher::instance().publish(notif);
    }

    void WorldSectorServiceImpl::onTransformChanged(uint64_t uuid, const glm::vec3& newPosition)
    {
        world::SectorCoord oldCoord = sectorManager.getEntitySector(uuid);
        world::SectorCoord newCoord = sectorManager.worldPositionToSectorCoord(newPosition);

        if (oldCoord == newCoord)
        {
            // Edit-mode moves within a sector must still mark it for save — otherwise
            // Save World skips the clean sector and silently drops the edit.
            // Play-mode motion must NOT dirty: dirty sectors are never auto-unloaded
            // by the streamer, so a wandering entity would pin its sector forever.
            if (!isPlayMode)
            {
                if (auto* sector = sectorManager.getSector(oldCoord))
                    sector->dirty = true;
            }
            return;
        }

        sectorManager.removeEntityFromSector(uuid, oldCoord);
        sectorManager.assignEntityToSector(uuid, newPosition);
    }

    glm::vec3 WorldSectorServiceImpl::getPrimaryCameraPosition() const
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto primaryCameraOpt = dispatcher.query(::events::scene::GetPrimaryCameraQuery{});
        if (!primaryCameraOpt.has_value())
            return cachedCameraPos;

        ::events::scene::GetWorldTransformQuery transformQuery;
        transformQuery.entity = *primaryCameraOpt;
        auto transformOpt = dispatcher.query(transformQuery);
        if (transformOpt.has_value())
            return transformOpt->position;

        return cachedCameraPos;
    }

    void WorldSectorServiceImpl::drawDebugSectors() const
    {
        if (!debugDrawSectors)
            return;

        auto& dispatcher = ::events::EventDispatcher::instance();
        auto& registry = scene::EntityRegistry::getRegistry();

        float sectorSize = sectorManager.getConfig().sectorWorldSize;

        sectorManager.forEachSector([&](const world::WorldSector& sector)
        {
            float cx = (static_cast<float>(sector.coord.x) + 0.5f) * sectorSize;
            float cz = (static_cast<float>(sector.coord.z) + 0.5f) * sectorSize;

            // Compute Y bounds from entity positions in this sector
            float yMin = 0.0f;
            float yMax = 10.0f;
            bool hasEntities = false;
            for (uint64_t uuid : sector.entityUUIDs)
            {
                auto ent = scene::EntityRegistry::findByUUID(uuid);
                if (ent != entt::null && registry.any_of<components::TransformComponent>(ent))
                {
                    float y = registry.get<components::TransformComponent>(ent).position.y;
                    if (!hasEntities)
                    {
                        yMin = y;
                        yMax = y + 1.0f;
                        hasEntities = true;
                    }
                    else
                    {
                        yMin = std::min(yMin, y);
                        yMax = std::max(yMax, y + 1.0f);
                    }
                }
            }
            // Add padding
            yMin -= 1.0f;
            yMax += 1.0f;

            float boxHeight = yMax - yMin;
            float cy = (yMin + yMax) * 0.5f;
            glm::vec3 center(cx, cy, cz);
            glm::vec3 halfExtents(sectorSize * 0.5f, boxHeight * 0.5f, sectorSize * 0.5f);

            glm::vec4 color;
            switch (sector.state)
            {
            case world::SectorState::Loaded:
                color = glm::vec4(0.2f, 0.9f, 0.2f, 1.0f); // Green
                break;
            case world::SectorState::Loading:
                color = glm::vec4(0.9f, 0.9f, 0.2f, 1.0f); // Yellow
                break;
            case world::SectorState::Unloading:
                color = glm::vec4(0.9f, 0.3f, 0.3f, 1.0f); // Red
                break;
            default:
                color = glm::vec4(0.5f, 0.5f, 0.5f, 0.6f); // Gray
                break;
            }

            ::events::debugdraw::DrawBoxCommand boxCmd;
            boxCmd.center = center;
            boxCmd.halfExtents = halfExtents;
            boxCmd.color = color;
            dispatcher.execute(boxCmd);
        });
    }

} // namespace services
