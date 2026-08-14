#include "WorldSectorServiceImpl.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "world/WorldSectorSerialization.hpp"
#include "world/HLODSerialization.hpp"
#include "world/SectorAssignment.hpp"
#include "serialization/SceneSerialization.hpp"
#include "serialization/SerializationFileAccess.hpp"
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
#include "math/TransformUtils.hpp"
#include "threading/JobSystem.hpp"
#include <algorithm>
#include <atomic>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace
{
    // ---- VK-1592: priority map for sector / HLOD LoadRequests ----
    //
    // ResourceLoadScheduler::computePriority is
    //     distanceFactor * importanceWeight + hint.priority
    // with distanceFactor = 1 / (1 + distance * 0.01) in (0, 1] and weights
    // Critical 5 / High 3 / Normal 1 / Low 0.5 / Background 0.2.
    //
    // Every existing asset load in the engine passes a DEFAULT LoadHint (Normal + 0.5), so the
    // whole texture/mesh/audio population sits at a flat 1.5; only shaders are Critical. The
    // additive bias below is what makes "activate-ring sectors outrank speculative asset
    // prefetch" unconditional instead of world-scale dependent: with a zero bias a sector
    // 1000 world units out would score ~0.27 and lose to a speculative texture.
    //
    //   activate ring : (0, 3]   + 1.5  ->  always above 1.5, ordered by distance among sectors
    //   HLOD proxy    : (0, 0.5] + 0.5  ->  0.5 .. 1.0, below every asset load, above prefetch
    //   prefetch ring : (0, 0.2] + 0.0  ->  below everything
    constexpr float kSectorActivateBias = 1.5f;
    constexpr float kHlodProxyBias = 0.5f;
    constexpr float kSectorPrefetchBias = 0.0f;

    // Synthetic AssetGUIDs. Sectors and HLOD cells are addressed by coord, never by GUID, and
    // never appear in the asset database - but leaving every request on AssetGUID::invalid()
    // would make findProgress()/cancel() ambiguous across concurrent sector loads. The top 16
    // bits mark the value as synthetic. The profiler resolves these rows through debugName
    // (TaskGraphWindow::loadDisplayName short-circuits on a non-empty name), so they never
    // reach a database lookup.
    constexpr uint64_t kSectorGuidTag = 0xFFF5'0000'0000'0000ull;
    constexpr uint64_t kHlodGuidTag = 0xFFF6'0000'0000'0000ull;

    // VK-1599: the grid is part of the identity, so profiler rows for the same coord on two grids
    // stay distinguishable. Grid 0 keeps the historical spelling.
    std::string sectorDebugName(const char* prefix, uint8_t gridIndex, const world::SectorCoord& coord)
    {
        std::string name(prefix);
        if (gridIndex != world::kPrimaryGridIndex)
            name += "[g" + std::to_string(static_cast<int>(gridIndex)) + "]";
        return name + "(" + std::to_string(coord.x) + "," + std::to_string(coord.z) + ")";
    }

    std::string hlodDebugName(const world::HLODCellCoord& cell)
    {
        return "hlod(" + std::to_string(cell.x) + "," + std::to_string(cell.z) + ",T" +
               std::to_string(static_cast<int>(cell.tier)) + ")";
    }

    // VK-1599: the grid index rides in bits 32-39, exactly where hlodGuidValue below puts the HLOD
    // tier over the same packed coord. The two families never collide - they carry different tags
    // in bits 48-63.
    asset::AssetGUID sectorLoadGuid(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        return asset::AssetGUID::fromValue(kSectorGuidTag | world::sectorRegistrationId(gridIndex, coord));
    }

    uint64_t hlodGuidValue(const world::HLODCellCoord& cell)
    {
        return kHlodGuidTag |
               (static_cast<uint64_t>(cell.tier) << 32) |
               world::sectorCoordToId(world::SectorCoord(cell.x, cell.z));
    }

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
        // VK-1593: the entity spawn budget below sits outside the streaming gate, so it must start
        // from the plain budget and only be widened on a frame the streamer actually bursted.
        // VK-1595: read through the effective config so a session override governs the live system.
        //
        // VK-1599: the entity spawn queue is shared, so the budget is the MAX over grids (see the
        // burst widening below for why max rather than sum). Edit-mode streaming is likewise a
        // whole-world switch: if ANY grid wants to stream in edit mode, the pass runs and the other
        // grids' streamers simply produce what their own radii ask for.
        int entityBudget = 0;
        bool anyEditModeStreaming = false;
        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
            const auto& config = getEffectiveStreamingConfig(gridIndex);
            entityBudget = std::max(entityBudget, config.maxEntitiesPerFrame);
            anyEditModeStreaming = anyEditModeStreaming || config.editModeStreaming;
        }

        if (isPlayMode || anyEditModeStreaming)
        {
            // Build streaming sources: camera is always source[0].
            // The editor camera isn't an ECS entity — use the cached viewport position.
            std::vector<world::StreamingSource> sources;
            {
                world::StreamingSource cameraSrc;
                if (isPlayMode)
                {
                    // VK-1593: only the play camera has a look direction to offer. The editor
                    // viewport reaches us through CameraPositionUpdatedNotification, which carries
                    // position only, so edit-mode streaming stays omni (viewDir zero).
                    const CameraPose pose = getPrimaryCameraPose();
                    cameraSrc.position = pose.position;
                    cameraSrc.viewDir = pose.forward;
                }
                else
                {
                    cameraSrc.position = cachedCameraPos;
                }
                cameraSrc.radiusMultiplier = 1.0f;
                cameraSrc.priority = 0;
                cameraSrc.id = 0;
                // VK-1595: the overlay centres on exactly this position, so the panel and the
                // rings it draws can never disagree with the ring the streamer resolved.
                lastStreamingOrigin = cameraSrc.position;
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

            // VK-1593: derive per-source velocity before the streamer runs. Script sources get
            // lookahead out of this for free - Streaming::updateWorldSource only ever pushes a
            // position, and the delta is all the lookahead needs.
            updateStreamingSourceVelocities(sources, deltaTime);

            // Update resource load scheduler with current camera position for priority re-computation
            resource::ResourceLoadScheduler::instance().update(sources[0].position);

            // VK-1599: one streamer per grid, all fed the SAME source list. Reach is per grid
            // because each streamer carries its own radii; the sources describe where the player
            // is, which is a property of the world, not of a grid.
            //
            // The per-frame load / prefetch / unload budgets are applied INSIDE
            // SectorStreamer::update, so every grid gets its own allowance for free.
            perGridActions.resize(grids.size());
            for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
            {
                auto& runtime = gridRuntime(gridIndex);
                runtime.streamer.update(sources, runtime.manager, perGridActions[gridIndex]);
            }
            world::mergeGridStreamingActions(perGridActions, streamingActions);

            // VK-1593: a camera jump invalidates the whole resident ring at once, so the entity
            // spawn budget has to widen with the load budget or the sectors arrive and then
            // trickle their entities in at 8 per frame anyway.
            //
            // VK-1599: SectorEntityLoader is a single shared spawn queue, so this budget is
            // inherently global. Take the MAX over grids rather than the sum - spawning is a
            // whole-process ECS cost, and summing would scale it with grid count. A grid that
            // bursts therefore gets its burst budget without the others inflating it further.
            for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
            {
                if (!gridRuntime(gridIndex).streamer.isBursting())
                    continue;
                entityBudget = std::max(
                    entityBudget,
                    world::effectiveBurstEntities(getEffectiveStreamingConfig(gridIndex)));
            }

            // Edit-mode rail: never auto-unload the sector holding the selected entity —
            // panels and gizmos hold live references to it.
            //
            // VK-1599: the selection lives on exactly one grid, and the reseed below has to fire on
            // THAT grid's streamer. Reseeding every grid would drop the ring state of grids that
            // had nothing to do with the selection.
            std::optional<world::SectorCoord> selectedCoord;
            uint8_t selectedGrid = world::kMaxGrids;
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
                            const uint64_t uuid = uuidComp->id.getValue();
                            selectedGrid = findEntityGrid(uuid);
                            if (selectedGrid < gridCount())
                                selectedCoord = gridRuntime(selectedGrid).manager.getEntitySector(uuid);
                        }
                    }
                }
            }

            for (const auto& merged : streamingActions)
            {
                const uint8_t gridIndex = merged.gridIndex;
                const auto& action = merged.action;
                auto& runtime = gridRuntime(gridIndex);

                const auto* actionSector = runtime.manager.getSector(action.coord);
                const world::SectorState currentState =
                    actionSector ? actionSector->state : world::SectorState::Unloaded;

                switch (action.target)
                {
                case world::SectorTargetState::Activated:
                    handleSectorLoad(gridIndex, action.coord);
                    break;

                case world::SectorTargetState::Prefetched:
                    handleSectorPrefetch(gridIndex, action.coord);
                    break;

                case world::SectorTargetState::Unloaded:
                    // VK-1591: a prefetched sector never spawned entities, never registered GPU
                    // objects or lights, and never published SectorActivated - so it must NOT go
                    // through handleSectorUnload, whose SectorDeactivated notification would tear
                    // down terrain tiles this sector never activated.
                    if (currentState == world::SectorState::Prefetching ||
                        currentState == world::SectorState::Prefetched)
                    {
                        handleSectorPrefetchDrop(gridIndex, action.coord);
                        break;
                    }
                    if (selectedCoord.has_value() && gridIndex == selectedGrid &&
                        action.coord == *selectedCoord)
                    {
                        // The streamer already dropped this coord from its tracking;
                        // force a reseed so it retries once the selection moves on
                        runtime.streamer.setEnabled(true);
                        break;
                    }
                    handleSectorUnload(gridIndex, action.coord);
                    break;
                }
            }

            // HLOD proxy streaming (beyond sector unload radius).
            //
            // VK-1599: bound to the PRIMARY grid. HLOD proxies stand in for landmarks the player
            // can see from far away, which is what grid 0 carries - the small-cell grids exist for
            // clutter that is gone long before a proxy would matter. Keeping one HLOD stack also
            // keeps one .vfHLOD naming convention and one bake inventory.
            if (worldDefinition.hlodConfig.enabled)
            {
                auto& primary = primaryRuntime();
                hlodActions.clear();
                // VK-1595: pause freezes DECISIONS on both rails - otherwise proxies would keep
                // popping in and out around the camera while the sector streamer sat frozen, which
                // is exactly the sector<->HLOD handoff the freeze exists to let you inspect. The
                // poll and the crossfade below are deliberately NOT gated: work already in flight
                // still completes, mirroring pollAsyncSectorLoads sitting outside the gate above.
                //
                // Pause applies to every grid at once (setPaused loops them), so reading the
                // primary grid's freeze state here speaks for all of them.
                if (!primary.streamer.isFrozen())
                {
                    hlodStreamer.update(sources, primary.manager,
                                       worldDefinition.primaryGrid().sectorConfig, hlodActions);
                }

                for (const auto& action : hlodActions)
                {
                    if (action.isLoad)
                    {
                        // VK-1594: a proxy that is mid fade-out is reversed rather than reloaded.
                        // submitHlodLoad would refuse the request (the proxy still exists) and
                        // HLODStreamer never re-emits a cell it has already claimed, so without
                        // this the cell would fade to nothing and stay blank for the session.
                        if (hlodProxyManager.getProxy(action.cellCoord) != nullptr)
                        {
                            hlodProxyManager.beginFadeIn(action.cellCoord);
                            continue;
                        }

                        const auto* tierConfig = findHLODTier(action.cellCoord.tier);
                        if (!tierConfig)
                            continue;

                        // VK-1594: the path comes from the per-cell inventory. The old code read
                        // it off the cell's CORNER sector, which is only ever correct for tier 0.
                        const std::string cellPath = resolveHLODCellPath(action.cellCoord, *tierConfig);
                        if (cellPath.empty())
                            continue;

                        // VK-1592: the read is submitted to the scheduler from here rather
                        // than inside HLODProxyManager, which lives in World.dll and would
                        // resolve ResourceLoadScheduler::instance() to a private, never
                        // pumped copy of the singleton (Utilities is a StaticLib).
                        const int32_t cs = world::effectiveCellSize(*tierConfig);
                        const world::SectorCoord origin =
                            world::cellOriginSector(action.cellCoord, *tierConfig);
                        const float sectorSize = primary.manager.getConfig().sectorWorldSize;
                        const float half = static_cast<float>(cs) * 0.5f;
                        const glm::vec3 cellCenter(
                            (static_cast<float>(origin.x) + half) * sectorSize, 0.0f,
                            (static_cast<float>(origin.z) + half) * sectorSize);
                        submitHlodLoad(action.cellCoord, cellPath, cellCenter);
                    }
                    else
                    {
                        // VK-1592: an in-flight read is no longer discarded implicitly by the
                        // proxies map (HLODProxyManager only learns about the cell once the
                        // bytes arrive), so cancel it explicitly or it would resurrect a proxy
                        // the streamer just dropped.
                        cancelHlodRequest(action.cellCoord);

                        const auto* proxy = hlodProxyManager.getProxy(action.cellCoord);
                        const bool visible = proxy &&
                            (proxy->state == world::HLODProxyState::Loaded ||
                             proxy->state == world::HLODProxyState::FadingIn);

                        if (visible)
                        {
                            // VK-1594: fade instead of hard-cutting. The destroy happens below,
                            // once the fade has actually run to completion.
                            hlodProxyManager.beginFadeOut(action.cellCoord);
                        }
                        else
                        {
                            // Nothing on screen yet (bytes still in flight, or no proxy at all),
                            // so there is nothing to fade - drop it immediately.
                            releaseHLODProxy(action.cellCoord);
                        }
                    }
                }

                // Poll before update() so a proxy whose bytes landed this frame still creates
                // its entities this frame, exactly as the old in-manager std::async did.
                pollAsyncHlodLoads();
                hlodProxyManager.update(*sceneGraph, deltaTime);

                // VK-1594: reap proxies whose fade-out finished on this tick.
                expiredHlodCells.clear();
                hlodProxyManager.collectExpiredProxies(expiredHlodCells);
                for (const auto& expired : expiredHlodCells)
                    releaseHLODProxy(expired);
            }
        }

        drawDebugSectors();

        // VK-1594: pump the async all-tier bake before the incremental re-bake queue - the queue
        // is gated on the baker being idle, so draining a finished bake here lets a queued
        // re-bake start on the very next frame instead of a frame later.
        hlodBaker.update();

        processHLODRegenQueue();

        entityLoader.update(*sceneGraph, entityBudget);

        // VK-1590: entities spawned this frame whose reference target was ALREADY resident.
        // Batched by the PostLoad hook so the whole spawn budget costs one resolver pass.
        if (!refTargetProbeQueue.empty())
        {
            referenceResolver.onSectorLoaded(refTargetProbeQueue);
            refTargetProbeQueue.clear();
        }

        bool anySectorBecameLoaded = false;

        // Transition sectors from Loading to Loaded once all their entities are processed.
        // VK-1599: per grid, and the grid index goes into every registration id and notification -
        // two grids can hold a sector at the same coord and their GPU slots must not alias.
        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
        gridRuntime(gridIndex).manager.forEachSector([&](world::WorldSector& sector)
        {
            if (sector.state == world::SectorState::Loading &&
                !entityLoader.hasPendingLoadsForSector(gridIndex, sector.coord))
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
                        cmd.sectorId = world::sectorRegistrationId(gridIndex, sector.coord);
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
                        cmd.sectorId = world::sectorRegistrationId(gridIndex, sector.coord);
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
                notif.gridIndex = gridIndex;
                notif.coord = sector.coord;
                notif.entityCount = static_cast<uint32_t>(sector.entityUUIDs.size());
                ::events::EventDispatcher::instance().publish(notif);

                // VK-1590: feed the resolver what is actually in the registry, expanded through
                // each payload's nested children — not the parse-time root list.
                referenceResolver.onSectorLoaded(expandLiveUUIDs(sector.entityUUIDs));
            }
        });
        }

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

    bool WorldSectorServiceImpl::loadSector(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        auto* sector = gridRuntime(gridIndex).manager.getSector(coord);
        if (!sector || sector->filePath.empty())
        {
            vfLogError("Cannot load sector ({},{}) on grid {}: no file path",
                       coord.x, coord.z, static_cast<int>(gridIndex));
            return false;
        }

        handleSectorLoad(gridIndex, coord);
        return true;
    }

    bool WorldSectorServiceImpl::unloadSector(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        auto* sector = gridRuntime(gridIndex).manager.getSector(coord);
        if (!sector)
            return false;

        // VK-1591: an explicit Unload must reach a prefetched sector too, but through the drop
        // path - it never activated, so it must not publish SectorDeactivated.
        if (sector->state == world::SectorState::Prefetching ||
            sector->state == world::SectorState::Prefetched)
        {
            handleSectorPrefetchDrop(gridIndex, coord);
            return true;
        }

        if (sector->state != world::SectorState::Loaded)
            return false;

        handleSectorUnload(gridIndex, coord);
        return true;
    }

    void WorldSectorServiceImpl::beginSectorActivation(uint8_t gridIndex, world::WorldSector& sector)
    {
        const world::SectorCoord coord = sector.coord;
        const world::SectorConfig& config = gridRuntime(gridIndex).manager.getConfig();

        // VK-1599: terrain tiles, ocean tiles and navmesh tiles are driven by the PRIMARY grid
        // only. Every grid overlaps the same ground, so letting each one drive them would
        // double-activate terrain tiles and unbalance the navmesh refcounts. The consumers gate on
        // this flag rather than on the grid index so the rule lives in one place.
        const bool drivesWorldSystems = (gridIndex == world::kPrimaryGridIndex);

        // Pre-notify subsystems (e.g. navmesh) so they can begin loading tiles
        // before the sector transitions to Loading and entities begin spawning
        {
            const float sectorSize = config.sectorWorldSize;
            ::events::world::SectorAboutToLoadNotification preNotif;
            preNotif.gridIndex = gridIndex;
            preNotif.drivesWorldSystems = drivesWorldSystems;
            preNotif.coord = coord;
            preNotif.sectorConfig = config;
            preNotif.boundsMin = glm::vec3(
                static_cast<float>(coord.x) * sectorSize, -1000.0f,
                static_cast<float>(coord.z) * sectorSize);
            preNotif.boundsMax = glm::vec3(
                static_cast<float>(coord.x + 1) * sectorSize, 1000.0f,
                static_cast<float>(coord.z + 1) * sectorSize);
            ::events::EventDispatcher::instance().publish(preNotif);
        }

        sector.state = world::SectorState::Loading;

        // Notify subsystems (e.g. terrain) that this sector is now active
        {
            ::events::world::SectorActivatedNotification notif;
            notif.gridIndex = gridIndex;
            notif.drivesWorldSystems = drivesWorldSystems;
            notif.coord = coord;
            notif.sectorConfig = config;
            ::events::EventDispatcher::instance().publish(notif);
        }
    }

    void WorldSectorServiceImpl::handleSectorLoad(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        auto& runtime = gridRuntime(gridIndex);
        auto* sector = runtime.manager.getSector(coord);
        if (!sector || sector->filePath.empty())
            return;

        // VK-1591: a Prefetching sector's read is already on this very queue. Promote in place:
        // publish the activation notifications the prefetch deliberately skipped and flip to
        // Loading. pollAsyncSectorLoads then sees wasPrefetch + state == Loading and hands the
        // arriving bytes straight to the parse hop - zero extra file IO.
        // MUST precede the pendingAsyncLoads.contains() guard below, which is true for exactly
        // this state and would otherwise strand the sector in Prefetching forever.
        if (sector->state == world::SectorState::Prefetching)
        {
            // VK-1592: that read was submitted at prefetch priority (below every asset load).
            // The player is now waiting on it, so lift it into the activate band before it
            // dispatches - otherwise a promoted sector would queue behind speculative texture
            // work. A no-op once the read is already running, which is the common case.
            resource::ResourceLoadScheduler::instance().reprioritize(
                sectorLoadGuid(gridIndex, coord),
                sectorLoadHint(gridIndex, coord, resource::LoadImportance::High, kSectorActivateBias));

            beginSectorActivation(gridIndex, *sector);
            return;
        }

        // Already have a pending async load for this sector
        if (runtime.pendingAsyncLoads.contains(coord))
            return;

        // VK-1591: bytes already resident — activate with zero file IO. Reuses the same async
        // queue and the same Loading -> finalizeSectorLoad -> Loaded path; only the source of the
        // bytes differs.
        if (sector->state == world::SectorState::Prefetched)
        {
            if (runtime.prefetchedBlobs.contains(coord))
            {
                beginSectorActivation(gridIndex, *sector);
                launchParseFromCachedBlob(gridIndex, coord);
                return;
            }
            // Cache lost (mode-change drop, byte-cap eviction): fall through to a file read
            sector->state = world::SectorState::Unloaded;
        }

        // VK-1592: no bespoke concurrency cap any more - the scheduler owns "how many at once",
        // and the activate ring's priority keeps it ahead of prefetches and asset loads.

        std::string filePath = sector->filePath;

        // VK-1591 AC #3 instrumentation: this is the COLD path. It must stay silent for a sector
        // the prefetch ring already brought in.
        vfLogDebug("Sector ({},{}) on grid {} activating with a file read (not prefetched)",
                   coord.x, coord.z, static_cast<int>(gridIndex));

        // Submit BEFORE publishing the activation notifications: a refused submit then leaves
        // nothing to unwind (no SectorActivated without a matching SectorDeactivated). Nothing
        // can observe the gap - the result is only ever consumed by pollAsyncSectorLoads, which
        // runs at the top of the next update().
        if (!submitSectorLoad(gridIndex, coord, resource::LoadImportance::High, kSectorActivateBias,
                sector->metadata.estimatedMemory, /*prefetchReservation=*/0,
                sectorDebugName("sector", gridIndex, coord),
                [filePath]() -> AsyncSectorLoadResult {
                    AsyncSectorLoadResult result;
                    result.success = world::WorldSectorSerialization::loadSector(
                        filePath, result.entityData, &result.dataLayers);
                    return result;
                }))
        {
            return; // already tracked; the streamer re-emits once it clears
        }

        beginSectorActivation(gridIndex, *sector);
    }

    void WorldSectorServiceImpl::handleSectorPrefetch(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        auto& runtime = gridRuntime(gridIndex);
        auto* sector = runtime.manager.getSector(coord);
        if (!sector || sector->filePath.empty())
            return;
        if (sector->state != world::SectorState::Unloaded)
            return;
        if (runtime.pendingAsyncLoads.contains(coord))
            return;

        // VK-1592: VK-1591's "reserve one of the four concurrency slots for a real activation" is
        // gone with the cap itself. Starvation is now prevented by priority instead - an
        // activate-ring sector scores above 1.5 while a prefetch scores at most 0.2, so the
        // scheduler's max-heap always dispatches activations first.
        //
        // The byte cap must count reads still in flight, though: without an in-flight ceiling a
        // burst would blow past maxPrefetchBytes by (in-flight x sector size) before the first
        // blob ever lands.
        //
        // VK-1599: the cap is accounted PER GRID. A shared pool would need a global eviction
        // policy to decide which grid gives bytes back, which is VK-1600's story; until then each
        // grid honours its own configured ceiling and the worst case is grids x cap.
        const uint64_t byteCap = getEffectiveStreamingConfig(gridIndex).maxPrefetchBytes; // VK-1595
        if (byteCap != 0 && runtime.prefetchedBytes + runtime.prefetchBytesInFlight >= byteCap)
            return;

        std::string filePath = sector->filePath;

        if (!submitSectorLoad(gridIndex, coord, resource::LoadImportance::Background, kSectorPrefetchBias,
                sector->metadata.estimatedMemory, sector->metadata.estimatedMemory,
                sectorDebugName("sector prefetch", gridIndex, coord),
                [filePath]() -> AsyncSectorLoadResult {
                    AsyncSectorLoadResult result;
                    result.wasPrefetch = true;
                    result.prefetchBytes = serialization::readSerializationFileBytes(filePath);
                    result.success = !result.prefetchBytes.empty();
                    return result;
                }))
        {
            return;
        }

        // Deliberately publishes NOTHING. SectorAboutToLoadNotification drives navmesh tile
        // pre-warm and SectorActivatedNotification drives TERRAIN TILE ACTIVATION - a prefetched
        // sector has no entities and must not pull terrain, or the prefetch ring would cost
        // exactly what it exists to avoid. Both fire on promotion, from beginSectorActivation.
        sector->state = world::SectorState::Prefetching;
    }

    void WorldSectorServiceImpl::launchParseFromCachedBlob(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        auto& runtime = gridRuntime(gridIndex);

        // Caller has already run beginSectorActivation(). Any refusal here MUST rewind the sector
        // to Unloaded: it is already Loading and the streamer never emits an action for a Loading
        // sector, so leaving it would strand it forever with nothing in flight.
        auto it = runtime.prefetchedBlobs.find(coord);
        if (it == runtime.prefetchedBlobs.end())
        {
            // Blob vanished between the caller's check and here.
            if (auto* sector = runtime.manager.getSector(coord))
                sector->state = world::SectorState::Unloaded;
            return;
        }

        std::vector<uint8_t> bytes = std::move(it->second);
        const uint64_t byteCount = bytes.size();
        runtime.prefetchedBytes -= byteCount;
        runtime.prefetchedBlobs.erase(it);

        // Same priority as a cold activation: this sector is in the activate ring either way,
        // it just skips the file read. estimatedBytes is the parse's own headroom (the blob is
        // already resident and was never in the gate's denominator, so this is not a double
        // count) - the DOM this produces is several times the byte count, so it errs low but in
        // the same spirit as ResourceLoadEstimate's conservative multipliers.
        if (!submitSectorLoad(gridIndex, coord, resource::LoadImportance::High, kSectorActivateBias,
                byteCount, /*prefetchReservation=*/0,
                sectorDebugName("sector parse", gridIndex, coord),
                [bytes = std::move(bytes)]() -> AsyncSectorLoadResult {
                    AsyncSectorLoadResult result;
                    result.success = world::WorldSectorSerialization::loadSectorFromMemory(
                        bytes, result.entityData, &result.dataLayers, "prefetched sector");
                    return result;
                }))
        {
            if (auto* sector = runtime.manager.getSector(coord))
                sector->state = world::SectorState::Unloaded;
        }
    }

    void WorldSectorServiceImpl::handleSectorPrefetchDrop(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        auto* sector = gridRuntime(gridIndex).manager.getSector(coord);
        if (!sector)
            return;

        cancelSectorRequest(gridIndex, coord);
        dropPrefetchedBlob(gridIndex, coord);
        sector->state = world::SectorState::Unloaded;
        // No notifications: nothing was ever activated, so nothing must be deactivated.
    }

    glm::vec3 WorldSectorServiceImpl::sectorCenterWorld(uint8_t gridIndex,
                                                         const world::SectorCoord& coord) const
    {
        // VK-1599: each grid has its own sectorWorldSize, so the same coord names a different patch
        // of world on each - the scheduler's distance-based priority would be wrong for every
        // non-primary grid if this read the primary grid's config.
        const float sectorSize = gridRuntime(gridIndex).manager.getConfig().sectorWorldSize;
        // y stays 0: the streamer's own distance test is 2D, and a uniform camera-height bias
        // shifts every sector equally, so it cannot reorder them.
        return glm::vec3((static_cast<float>(coord.x) + 0.5f) * sectorSize, 0.0f,
                         (static_cast<float>(coord.z) + 0.5f) * sectorSize);
    }

    resource::LoadHint WorldSectorServiceImpl::sectorLoadHint(uint8_t gridIndex,
                                                               const world::SectorCoord& coord,
                                                               resource::LoadImportance importance,
                                                               float hintPriority) const
    {
        resource::LoadHint hint;
        hint.worldPosition = sectorCenterWorld(gridIndex, coord);
        hint.importance = importance;
        hint.priority = hintPriority;
        hint.sectorId = world::sectorRegistrationId(gridIndex, coord);
        return hint;
    }

    bool WorldSectorServiceImpl::submitSectorLoad(uint8_t gridIndex,
                                                   const world::SectorCoord& coord,
                                                   resource::LoadImportance importance,
                                                   float hintPriority,
                                                   uint64_t estimatedBytes,
                                                   uint64_t prefetchReservation,
                                                   std::string debugName,
                                                   std::function<AsyncSectorLoadResult()> work)
    {
        auto& runtime = gridRuntime(gridIndex);
        auto slot = std::make_shared<SectorLoadSlot>();

        // Track the future FIRST. When the JobSystem is uninitialised (Tests.exe) the scheduler
        // runs executeLoad inline inside submit(), so the slot can already be fulfilled by the
        // time submit() returns - launch() must have taken the future before that.
        if (!runtime.pendingAsyncLoads.launch(coord, slot->getFuture()))
            return false;

        auto cancellation = resource::CancellationToken::create();
        runtime.sectorRequests[coord] = PendingSectorRequest{slot, cancellation, prefetchReservation};
        runtime.prefetchBytesInFlight += prefetchReservation;

        resource::LoadRequest request;
        request.guid = sectorLoadGuid(gridIndex, coord);
        request.hint = sectorLoadHint(gridIndex, coord, importance, hintPriority);
        request.assetType = resource::AssetType::WorldSector;
        request.estimatedBytes = estimatedBytes;
        request.debugName = std::move(debugName);
        request.cancellation = cancellation;
        // `slot` is captured by VALUE here on purpose: this lambda holds the ONLY strong
        // reference. Every way the scheduler can drop the request without running us - queue-full
        // rejection, priority eviction, the cancelled-pending sweep in update() - destroys the
        // lambda and lets ~AsyncResultSlot resolve the future, so poll()/drain() never see a
        // broken promise.
        request.executeLoad = [slot, cancellation, work = std::move(work)]()
        {
            // ResourceLoadScheduler::shutdown() moves pending lambdas out and runs them directly,
            // bypassing the dispatch-time cancellation check - without this a teardown would pay
            // for a full sector read on the calling thread.
            if (cancellation->isCancelled())
            {
                slot->fulfil(AsyncSectorLoadResult{}); // abandoned by default
                return;
            }
            AsyncSectorLoadResult result = work();
            result.abandoned = false; // it ran; success now means what it says
            slot->fulfil(std::move(result));
        };

        resource::ResourceLoadScheduler::instance().submit(std::move(request));
        return true;
    }

    void WorldSectorServiceImpl::releaseSectorRequest(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        auto& runtime = gridRuntime(gridIndex);
        auto it = runtime.sectorRequests.find(coord);
        if (it == runtime.sectorRequests.end())
            return;

        runtime.prefetchBytesInFlight -= it->second.prefetchReservation;
        runtime.sectorRequests.erase(it);
    }

    void WorldSectorServiceImpl::cancelSectorRequest(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        auto& runtime = gridRuntime(gridIndex);
        auto it = runtime.sectorRequests.find(coord);
        if (it != runtime.sectorRequests.end())
        {
            it->second.cancellation->cancel();
            // Resolve the future NOW rather than waiting for the scheduler to notice. A request
            // still sitting in the scheduler's pending map has no worker behind it, so a later
            // AsyncLoadQueue::drain() would block on it forever. fulfil() is idempotent, so a
            // worker already running concurrently simply loses the race harmlessly.
            if (auto slot = it->second.slot.lock())
            {
                slot->fulfil(AsyncSectorLoadResult{}); // abandoned by default
            }
            runtime.prefetchBytesInFlight -= it->second.prefetchReservation;
            runtime.sectorRequests.erase(it);
        }

        runtime.pendingAsyncLoads.cancel(coord);
    }

    void WorldSectorServiceImpl::drainSectorLoads()
    {
        // Every outstanding request must be resolved BEFORE AsyncLoadQueue::drain(), which blocks
        // on future.get() for each tracked entry. Loads already executing on a worker still
        // complete normally (exactly as the old std::async drain did); the ones this unblocks are
        // those the scheduler has not dispatched yet, which would otherwise never satisfy their
        // promise and would hang the main thread.
        //
        // VK-1599: EVERY grid. This is only called when the world itself is changing underneath -
        // load, create, clear, play/stop - and a request left in flight on any grid would land in
        // a later poll and be attributed to the new world's sector at the same coord.
        for (auto& grid : grids)
        {
            for (auto& [coord, request] : grid->sectorRequests)
            {
                request.cancellation->cancel();
                if (auto slot = request.slot.lock())
                {
                    slot->fulfil(AsyncSectorLoadResult{}); // abandoned by default
                }
            }
            grid->sectorRequests.clear();
            grid->prefetchBytesInFlight = 0;

            grid->pendingAsyncLoads.drain();
        }
    }

    void WorldSectorServiceImpl::submitHlodLoad(const world::HLODCellCoord& cell,
                                                 const std::string& filePath,
                                                 const glm::vec3& cellCenter)
    {
        if (hlodProxyManager.getProxy(cell) != nullptr)
            return; // already resident

        auto slot = std::make_shared<HlodLoadSlot>();
        if (!pendingHlodLoads.launch(cell, slot->getFuture()))
            return;

        auto cancellation = resource::CancellationToken::create();
        hlodRequests[cell] = PendingHlodRequest{slot, cancellation};

        resource::LoadRequest request;
        request.guid = asset::AssetGUID::fromValue(hlodGuidValue(cell));
        request.hint.worldPosition = cellCenter;
        request.hint.importance = resource::LoadImportance::Low;
        request.hint.priority = kHlodProxyBias;
        request.assetType = resource::AssetType::WorldSector;
        // HLODFileHeader carries no total size and nothing caches .vfHLOD headers, so any
        // estimate would cost a second read. 0 means "undeterminable -> ungated by design",
        // the same convention ResourceLoadEstimate uses for an unknown size.
        request.estimatedBytes = 0;
        request.debugName = hlodDebugName(cell);
        request.cancellation = cancellation;
        request.executeLoad = [slot, cancellation, filePath]()
        {
            AsyncHlodLoadResult result;
            if (cancellation->isCancelled())
            {
                slot->fulfil(std::move(result)); // abandoned by default
                return;
            }
            result.abandoned = false;
            result.success = world::HLODSerialization::load(filePath, result.data);
            slot->fulfil(std::move(result));
        };

        resource::ResourceLoadScheduler::instance().submit(std::move(request));
    }

    void WorldSectorServiceImpl::cancelHlodRequest(const world::HLODCellCoord& cell)
    {
        auto it = hlodRequests.find(cell);
        if (it != hlodRequests.end())
        {
            it->second.cancellation->cancel();
            if (auto slot = it->second.slot.lock())
            {
                slot->fulfil(AsyncHlodLoadResult{}); // abandoned by default
            }
            hlodRequests.erase(it);
        }

        pendingHlodLoads.cancel(cell);
    }

    void WorldSectorServiceImpl::releaseHLODProxy(const world::HLODCellCoord& cell)
    {
        // VK-1594: destroy the entities AND free the GPU geometry. Unlike a normal mesh there is
        // no .vfMesh to re-stream from, so MeshStreamManager pins in-memory meshes and the release
        // has to be explicit - the per-frame unrequestMesh sweep will never reclaim it.
        std::string meshKey;
        if (const auto* proxy = hlodProxyManager.getProxy(cell))
            meshKey = proxy->meshKey;

        hlodProxyManager.unloadProxy(cell, *sceneGraph);

        if (!meshKey.empty())
        {
            ::events::render::objectstreaming::ReleaseHLODMeshCommand relCmd;
            relCmd.meshKey = meshKey;
            try
            {
                ::events::EventDispatcher::instance().execute(relCmd);
            }
            catch (const std::exception&)
            {
                // No handler outside the Editor - nothing was ever registered, so nothing to free.
            }
        }
    }

    void WorldSectorServiceImpl::pollAsyncHlodLoads()
    {
        pendingHlodLoads.poll([this](const world::HLODCellCoord& cell, AsyncHlodLoadResult result)
        {
            hlodRequests.erase(cell);

            if (!result.success)
            {
                // HLODStreamer claims a cell in loadedProxies the moment it emits the load action
                // and never re-emits, so a dropped or failed read would lose this proxy for the
                // rest of the session. forgetProxy releases the claim.
                hlodStreamer.forgetProxy(cell);
                if (!result.abandoned)
                {
                    vfLogError("HLOD proxy load failed for cell [{},{},T{}]",
                               cell.x, cell.z, static_cast<int>(cell.tier));
                }
                return;
            }

            // VK-1594: push the geometry to the GPU before handing the data to the proxy manager.
            // The command borrows `result.data` for the duration of a synchronous dispatch, so it
            // has to happen before the move below. The mesh key is the .vfHLOD path, which is what
            // the proxy entity's MeshComponent will resolve to.
            const auto* tierConfig = findHLODTier(cell.tier);
            const std::string meshKey = tierConfig ? resolveHLODCellPath(cell, *tierConfig) : std::string{};

            bool registered = false;
            if (!meshKey.empty())
            {
                ::events::render::objectstreaming::RegisterHLODMeshCommand regCmd;
                regCmd.meshKey = meshKey;
                regCmd.data = &result.data;
                try
                {
                    registered = ::events::EventDispatcher::instance().execute(regCmd);
                }
                catch (const std::exception&)
                {
                    // EventDispatcher::execute THROWS when nothing registered the command, and
                    // ObjectStreamingServiceImpl is wired by EditorServiceBootstrap only - so this
                    // is the normal state in Runtime and in Tests.exe. Degrade to "no HLOD
                    // geometry" rather than taking down the streaming update.
                    registered = false;
                }
            }

            if (!registered)
            {
                // No GPU-driven renderer (or a corrupt bake): spawning an entity whose
                // MeshComponent resolves to nothing would just add an invisible, uncullable
                // object, so drop the cell and let the streamer re-emit it later.
                hlodStreamer.forgetProxy(cell);
                return;
            }

            hlodProxyManager.loadProxyFromData(cell, meshKey, std::move(result.data));
        });
    }

    void WorldSectorServiceImpl::drainHlodLoads()
    {
        for (auto& [cell, request] : hlodRequests)
        {
            request.cancellation->cancel();
            if (auto slot = request.slot.lock())
            {
                slot->fulfil(AsyncHlodLoadResult{}); // abandoned by default
            }
            // drain() below discards results without running the poll callback, so release the
            // streamer's claim here instead - HLODStreamer inserts a cell into loadedProxies at
            // emit time and would never re-emit an abandoned one.
            hlodStreamer.forgetProxy(cell);
        }
        hlodRequests.clear();

        pendingHlodLoads.drain();
    }

    void WorldSectorServiceImpl::dropPrefetchedBlob(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        auto& runtime = gridRuntime(gridIndex);
        auto it = runtime.prefetchedBlobs.find(coord);
        if (it == runtime.prefetchedBlobs.end())
            return;
        runtime.prefetchedBytes -= it->second.size();
        runtime.prefetchedBlobs.erase(it);
    }

    void WorldSectorServiceImpl::clearPrefetchedBlobs()
    {
        // VK-1599: every grid, same argument as drainSectorLoads - blobs are keyed on a bare
        // SectorCoord within a grid, and this only runs when the world is changing underneath.
        for (auto& grid : grids)
        {
            grid->prefetchedBlobs.clear();
            grid->prefetchedBytes = 0;
        }
    }

    void WorldSectorServiceImpl::pollAsyncSectorLoads()
    {
        // VK-1591: a prefetch promoted to Activated mid-flight needs its parse hop launched, but
        // AsyncLoadQueue::poll keeps using its iterator after the callback returns and launch()
        // inserts into the same map. Collect here, drain after poll() finishes.
        std::vector<world::SectorCoord> promotedPrefetches;

        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
            auto& runtime = gridRuntime(gridIndex);
            promotedPrefetches.clear();

            runtime.pendingAsyncLoads.poll([this, gridIndex, &runtime, &promotedPrefetches](
                                               const world::SectorCoord& coord,
                                               AsyncSectorLoadResult result)
            {
                releaseSectorRequest(gridIndex, coord);

                auto* sector = runtime.manager.getSector(coord);
                if (!sector)
                    return;

                // VK-1592: the scheduler dropped this request before it ever ran (queue eviction,
                // scheduler shutdown). Not an error - rewind and let the streamer re-emit.
                if (result.abandoned)
                {
                    sector->state = world::SectorState::Unloaded;
                    vfLogDebug("Sector ({},{}) on grid {} load abandoned by the scheduler; will retry",
                               coord.x, coord.z, static_cast<int>(gridIndex));
                    return;
                }

                if (!result.success)
                {
                    sector->state = world::SectorState::Unloaded;
                    vfLogError("Async sector {} failed for ({},{}) on grid {}",
                               result.wasPrefetch ? "prefetch" : "load", coord.x, coord.z,
                               static_cast<int>(gridIndex));
                    return;
                }

                if (result.wasPrefetch)
                {
                    // Bytes only. entityUUIDs is NOT populated and no
                    // SectorDataLayerLoadedNotification is published - a prefetched sector has no
                    // entities and its data layers are not live yet. Both happen in
                    // finalizeSectorLoad, on activation.
                    // Drop first so the running total can never drift: assigning over an existing
                    // entry would silently leak its bytes out of the accounting.
                    dropPrefetchedBlob(gridIndex, coord);
                    runtime.prefetchedBytes += result.prefetchBytes.size();
                    runtime.prefetchedBlobs[coord] = std::move(result.prefetchBytes);

                    if (sector->state == world::SectorState::Loading)
                        promotedPrefetches.push_back(coord); // promoted while the read was in flight
                    else
                        sector->state = world::SectorState::Prefetched;
                    return;
                }

                finalizeSectorLoad(gridIndex, coord, result.entityData, result.dataLayers);
            });

            for (const auto& coord : promotedPrefetches)
                launchParseFromCachedBlob(gridIndex, coord);
        }
    }

    void WorldSectorServiceImpl::finalizeSectorLoad(uint8_t gridIndex,
                                                     const world::SectorCoord& coord,
                                                     std::vector<nlohmann::json>& entityData,
                                                     world::SectorDataLayers& dataLayers)
    {
        auto* sector = gridRuntime(gridIndex).manager.getSector(coord);
        if (!sector)
            return;

        // Merge file layers under any in-memory ones: layers written at runtime
        // (e.g. fog-of-war) survive unload on the sector struct and are newer
        // than what the file holds.
        //
        // VK-1596: the merge also reports whether anything resident is NOT in the file - see the
        // dirty assignment at the end of this function.
        const bool unsavedLayers = world::mergeSectorDataLayers(sector->dataLayers, dataLayers);
        for (const auto& [layerName, bytes] : sector->dataLayers)
        {
            ::events::world::SectorDataLayerLoadedNotification notif;
            notif.gridIndex = gridIndex;
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

        entityLoader.queueSectorLoadFromData(gridIndex, coord, entityNamesAndJson);

        // State stays Loading until all entities are processed (checked in update())
        //
        // VK-1596: normally "just loaded from disk - nothing to save", but NOT when the merge above
        // preserved a resident layer the file does not hold (or holds with different bytes). An
        // explicit UnloadSectorCommand from the grid bypasses the streamer's dirty guard, so
        // create-layer -> unload -> reload used to land here with the layer still resident and the
        // dirty flag cleared - after which Save World skipped the sector and the layer was never
        // written. Play mode still must never dirty: that would pin the sector against unload.
        sector->dirty = unsavedLayers && !isPlayMode;
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

    void WorldSectorServiceImpl::handleSectorUnload(uint8_t gridIndex, const world::SectorCoord& coord)
    {
        auto& runtime = gridRuntime(gridIndex);
        auto* sector = runtime.manager.getSector(coord);
        if (!sector)
            return;

        // Cancel any in-flight async file I/O for this sector. VK-1592: a request still queued in
        // the scheduler is dropped outright and its future resolved immediately; one already
        // executing runs to completion and its result is discarded on the next poll.
        cancelSectorRequest(gridIndex, coord);
        // VK-1591: and drop any prefetch blob. A sector reaching this path was activated, so its
        // blob was already consumed by launchParseFromCachedBlob — but a promotion that failed
        // mid-flight can leave one behind, and a stale blob would resurrect old content.
        dropPrefetchedBlob(gridIndex, coord);

        sector->state = world::SectorState::Unloading;

        // Notify subsystems (e.g. terrain) that this sector is now inactive
        {
            ::events::world::SectorDeactivatedNotification notif;
            notif.gridIndex = gridIndex;
            notif.drivesWorldSystems = (gridIndex == world::kPrimaryGridIndex);
            notif.coord = coord;
            notif.sectorConfig = runtime.manager.getConfig();
            ::events::EventDispatcher::instance().publish(notif);
        }

        // Unregister sector objects and lights before entities are destroyed
        {
            events::render::objectstreaming::UnregisterSectorObjectsCommand objCmd;
            objCmd.sectorId = world::sectorRegistrationId(gridIndex, coord);
            ::events::EventDispatcher::instance().execute(objCmd);
        }
        {
            events::render::lightstreaming::UnregisterSectorLightsCommand cmd;
            cmd.sectorId = world::sectorRegistrationId(gridIndex, coord);
            ::events::EventDispatcher::instance().execute(cmd);
        }

        // Cancel any pending entity loads for this sector (prevents recreating entities after unload)
        entityLoader.cancelPendingLoads(gridIndex, coord);

        // Separate static entities (to unload) from entities that must stay alive: dynamic ones,
        // and — VK-1597 — anything pinned as not spatially loaded.
        std::vector<uint64_t> staticUUIDs;
        std::vector<uint64_t> retainedUUIDs;

        for (uint64_t uuid : sector->entityUUIDs)
        {
            bool retain = false;
            auto ent = scene::EntityRegistry::findByUUID(uuid);
            if (ent != entt::null)
            {
                scene::Entity sceneEntity(ent);
                if (sceneEntity.hasComponent<components::TransformComponent>())
                {
                    retain = !sceneEntity.getComponent<components::TransformComponent>().isStatic;
                }

                // VK-1597: the guarantee behind the whole feature, enforced at the one point where
                // it could be violated. A pinned entity can still be listed here - a .vfsector
                // written before it was pinned lists it, and reconcileAlwaysLoadedEntities does not
                // run until the next Save World - so destroying it on that word alone would be
                // exactly the bug the flag exists to prevent.
                retain = retain || !world::readEntityStreamingTraits(sceneEntity).spatiallyLoaded;
            }

            if (retain)
                retainedUUIDs.push_back(uuid);
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
        entityLoader.queueSectorUnload(gridIndex, coord, staticUUIDs);

        // Clear the sector's entity list, then re-add the entities that stayed alive so they
        // remain tracked
        sector->entityUUIDs = retainedUUIDs;
        sector->state = world::SectorState::Unloaded;

        ::events::world::SectorUnloadedNotification notif;
        notif.gridIndex = gridIndex;
        notif.drivesWorldSystems = (gridIndex == world::kPrimaryGridIndex);
        notif.coord = coord;
        notif.sectorConfig = runtime.manager.getConfig();
        ::events::EventDispatcher::instance().publish(notif);
    }

    void WorldSectorServiceImpl::onTransformChanged(uint64_t uuid, const glm::vec3& newPosition)
    {
        // VK-1599: an entity belongs to exactly one grid, and only that grid's manager buckets it.
        // Its grid never changes on a move - a grid change is a StreamingPolicyComponent edit,
        // handled by onStreamingPolicyChanged - so resolve the owning grid and stay on it.
        const uint8_t gridIndex = findEntityGrid(uuid);
        if (gridIndex >= gridCount())
            return;

        auto& manager = gridRuntime(gridIndex).manager;
        const world::SectorCoord oldCoord = manager.getEntitySector(uuid);
        const world::SectorCoord newCoord = manager.worldPositionToSectorCoord(newPosition);

        if (oldCoord == newCoord)
        {
            // Edit-mode moves within a sector must still mark it for save — otherwise
            // Save World skips the clean sector and silently drops the edit.
            // Play-mode motion must NOT dirty: dirty sectors are never auto-unloaded
            // by the streamer, so a wandering entity would pin its sector forever.
            if (!isPlayMode)
            {
                if (auto* sector = manager.getSector(oldCoord))
                    sector->dirty = true;
            }
            return;
        }

        manager.removeEntityFromSector(uuid, oldCoord);
        manager.assignEntityToSector(uuid, newPosition);
    }

    uint8_t WorldSectorServiceImpl::findEntityGrid(uint64_t uuid) const
    {
        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
            if (gridRuntime(gridIndex).manager.hasEntitySector(uuid))
                return gridIndex;
        }
        return world::kMaxGrids;
    }

    std::vector<world::SectorConfig> WorldSectorServiceImpl::gridSectorConfigs() const
    {
        std::vector<world::SectorConfig> configs;
        configs.reserve(worldDefinition.grids.size());
        for (const auto& grid : worldDefinition.grids)
            configs.push_back(grid.sectorConfig);
        return configs;
    }

    world::SectorAssignment WorldSectorServiceImpl::resolveEntityAssignment(
        const scene::Entity& entity) const
    {
        return world::resolveSectorAssignment(world::readEntityStreamingTraits(entity),
                                              gridSectorConfigs());
    }

    void WorldSectorServiceImpl::onStreamingPolicyChanged(uint64_t uuid, bool spatiallyLoaded)
    {
        // VK-1597: edit-mode only, same reason as MarkEntitySectorDirtyCommand and
        // onTransformChanged above - a dirty sector is never auto-unloaded, so migrating during
        // play would pin sectors for the rest of the session. The play->edit restore re-buckets
        // every root child through the same gate, so a flip attempted in play is honoured on stop.
        if (isPlayMode)
            return;

        // VK-1599: a policy edit can change the entity's GRID as well as its spatially-loaded
        // flag, so the entity is unbucketed from wherever it currently lives and re-resolved from
        // scratch. Doing it in that order means a grid change is just an unbucket followed by a
        // bucket, and needs no separate path.
        const uint8_t currentGrid = findEntityGrid(uuid);
        if (currentGrid < gridCount())
        {
            auto& manager = gridRuntime(currentGrid).manager;
            const world::SectorCoord coord = manager.getEntitySector(uuid);
            auto* sector = manager.getSector(coord);

            // canMigrateEntity is a DATA-LOSS guard, not a convenience check: a non-Loaded
            // sector's entityUUIDs is empty or holds only its dynamic leftovers, so dirtying it
            // would make the next Save World overwrite a good .vfsector with an empty one. This
            // is reachable - handleSectorUnload leaves dynamic entities mapped to an Unloaded
            // sector.
            if (!sector || !world::canMigrateEntity(*sector))
            {
                vfLogWarning("[WorldSector] Entity {} is mapped to sector ({},{}) on grid {}, which "
                             "is not loaded - its streaming policy will be applied on the next Save "
                             "World instead.",
                             uuid, coord.x, coord.z, static_cast<int>(currentGrid));
                return;
            }

            manager.removeEntityFromSector(uuid, coord);
            if (!spatiallyLoaded)
            {
                ++alwaysLoadedMigrationCount;
                return;
            }
        }
        else if (!spatiallyLoaded)
        {
            return; // already unbucketed and staying that way
        }

        // Re-derive the assignment rather than trusting the old coord: the entity may have been
        // moved while it was pinned (TransformChangedNotification ignores entities with no sector),
        // and its grid may have just changed.
        auto entity = scene::EntityRegistry::findByUUID(uuid);
        if (entity == entt::null)
            return;

        const scene::Entity sceneEntity(entity);
        const auto assignment = resolveEntityAssignment(sceneEntity);
        if (!assignment.isSpatial())
            return;

        gridRuntime(assignment.gridIndex).manager.assignEntityToSector(uuid, assignment.coord);
    }

    uint32_t WorldSectorServiceImpl::reconcileAlwaysLoadedEntities()
    {
        uint32_t migrated = 0;

        // VK-1599: (grid, uuid, coord). An entity is only ever bucketed on one grid, but the sweep
        // has to visit every grid to find it.
        struct PendingRemoval
        {
            uint8_t gridIndex;
            uint64_t uuid;
            world::SectorCoord coord;
            // True when the entity is leaving the sector system entirely (pinned always-loaded)
            // rather than merely moving to another grid. Only the former counts as a migration:
            // the World Sectors window uses that count to warn that a Save Scene is still owed,
            // and a grid move is written by Save World like any other sector edit.
            bool leavingSectors;
        };
        std::vector<PendingRemoval> toRemove;

        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
            gridRuntime(gridIndex).manager.forEachSector([&](world::WorldSector& sector)
            {
                // Loaded only. An Unloaded/Prefetched sector's entityUUIDs is not authoritative,
                // and resolving it against the registry would "find" nothing and drop real
                // entities.
                if (!world::canMigrateEntity(sector))
                    return;

                for (uint64_t uuid : sector.entityUUIDs)
                {
                    auto entity = scene::EntityRegistry::findByUUID(uuid);
                    if (entity == entt::null)
                        continue;

                    const scene::Entity sceneEntity(entity);
                    const auto traits = world::readEntityStreamingTraits(sceneEntity);

                    // Two reasons to pull an entity out of this sector: it is pinned
                    // always-loaded, or VK-1599 moved it onto a different grid and the sector file
                    // it was written into still lists it.
                    if (!traits.spatiallyLoaded)
                        toRemove.push_back({gridIndex, uuid, sector.coord, true});
                    else if (worldDefinition.clampGridIndex(traits.gridIndex) != gridIndex)
                        toRemove.push_back({gridIndex, uuid, sector.coord, false});
                }
            });
        }

        // Deduplicate on uuid rather than on hasEntitySector: a pinned entity spawned from a stale
        // .vfsector is deliberately never registered in entityToSector (see the setOnEntityLoaded
        // gate), so a hasEntitySector check would skip exactly the entries this pass exists to
        // clean up. removeEntityFromSector erases every occurrence, which is what makes the second
        // visit of a duplicated uuid a harmless no-op.
        std::unordered_set<uint64_t> handled;
        for (const auto& pending : toRemove)
        {
            if (!handled.insert(pending.uuid).second)
                continue;

            gridRuntime(pending.gridIndex).manager.removeEntityFromSector(pending.uuid, pending.coord);

            if (pending.leavingSectors)
            {
                ++migrated;
                continue;
            }

            // A grid change is a move, not a migration out of the world - re-bucket it on the grid
            // it now names so Save World writes it into that grid's sector set.
            auto entity = scene::EntityRegistry::findByUUID(pending.uuid);
            if (entity == entt::null)
                continue;

            const scene::Entity sceneEntity(entity);
            const auto assignment = resolveEntityAssignment(sceneEntity);
            if (assignment.isSpatial())
                gridRuntime(assignment.gridIndex).manager.assignEntityToSector(pending.uuid, assignment.coord);
        }

        return migrated;
    }

    WorldSectorServiceImpl::CameraPose WorldSectorServiceImpl::getPrimaryCameraPose() const
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        CameraPose pose;
        pose.position = cachedCameraPos;

        auto primaryCameraOpt = dispatcher.query(::events::scene::GetPrimaryCameraQuery{});
        if (!primaryCameraOpt.has_value())
            return pose;

        ::events::scene::GetWorldTransformQuery transformQuery;
        transformQuery.entity = *primaryCameraOpt;
        auto transformOpt = dispatcher.query(transformQuery);
        if (transformOpt.has_value())
        {
            pose.position = transformOpt->position;
            // VK-1593: the engine's single source of truth for forward-from-Euler-degrees,
            // shared with AudioAPI and AudioSceneUpdater. Do not re-derive it here.
            pose.forward = math::forwardFromEulerDegrees(transformOpt->rotation);
        }

        return pose;
    }

    void WorldSectorServiceImpl::updateStreamingSourceVelocities(
        std::vector<world::StreamingSource>& sources, float deltaTime)
    {
        sourceVelocityScratch.clear();

        // Two updates inside one clock tick give a dt of a few hundred nanoseconds, which turns a
        // one-unit drift into a velocity of millions. The streamer clamps the resulting offset to
        // the outer ring, so the symptom is not a blow-up but something subtler and worse: the
        // prediction silently pins at maximum range while the source is barely moving. A frame
        // shorter than 10us is a clock artefact, not motion.
        constexpr float kMinDeltaTime = 1e-5f;

        for (auto& source : sources)
        {
            auto it = lastSourcePositionsForVelocity.find(source.id);
            if (it != lastSourcePositionsForVelocity.end() && deltaTime > kMinDeltaTime)
            {
                // No teleport guard here on purpose: a jump produces an absurd velocity, and
                // SectorStreamer discards it on exactly the frames it classifies as a jump. One
                // guard, in the layer that has CPU tests.
                source.velocity = (source.position - it->second) / deltaTime;
            }
            else
            {
                source.velocity = glm::vec3(0.0f);
            }

            sourceVelocityScratch[source.id] = source.position;
        }

        // Swap rather than erase-missing: an unregistered source is simply absent from the
        // scratch map, so it drops out without a sweep and re-registering starts clean.
        lastSourcePositionsForVelocity.swap(sourceVelocityScratch);
    }

    void WorldSectorServiceImpl::drawDebugSectors() const
    {
        if (!debugDrawSectors)
            return;

        auto& dispatcher = ::events::EventDispatcher::instance();
        auto& registry = scene::EntityRegistry::getRegistry();

        // VK-1599: every grid draws its own boxes at its own cell size. Overlapping outlines are
        // exactly the point - seeing a coarse landmark grid and a fine clutter grid on top of each
        // other is what "Show Sector Bounds" is for once a world has more than one.
        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
        const float sectorSize = gridRuntime(gridIndex).manager.getConfig().sectorWorldSize;

        gridRuntime(gridIndex).manager.forEachSector([&](const world::WorldSector& sector)
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
            case world::SectorState::Prefetching:
                color = glm::vec4(0.2f, 0.7f, 0.8f, 0.8f); // Dim cyan — VK-1591 bytes in flight
                break;
            case world::SectorState::Prefetched:
                color = glm::vec4(0.2f, 0.85f, 0.95f, 1.0f); // Cyan — VK-1591 bytes resident
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
    }

} // namespace services
