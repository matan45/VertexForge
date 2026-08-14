#pragma once

#include "../../interfaces/world/IWorldSectorService.hpp"
#include "../../events/EventTypes.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/animation/AnimationSnapshotEvents.hpp"
#include "world/WorldSectorManager.hpp"
#include "world/SectorDataLayerOps.hpp"
#include "world/SectorRepartitionPlanner.hpp"
#include "world/WorldDefinition.hpp"
#include "world/SectorStreamer.hpp"
#include "world/GridActionMerge.hpp"
#include "world/SectorAssignment.hpp" // VK-1599: SectorAssignment, returned by resolveEntityAssignment
#include "world/SectorEntityLoader.hpp"
#include "world/PendingReferenceResolver.hpp"
#include "world/SectorRefFieldRegistry.hpp"
#include "world/HLODStreamer.hpp"
#include "world/HLODProxyManager.hpp"
#include "world/HLODSerialization.hpp"
#include "HLODWorldBaker.hpp"
#include "streaming/AsyncLoadQueue.hpp"
#include "streaming/AsyncResultSlot.hpp"
#include "resource/CancellationToken.hpp"
#include "resource/ResourceLoadTypes.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace scene
{
    class SceneGraphSystem;
}

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class WorldSectorServiceImpl : public IWorldSectorService
    {
    public:
        explicit WorldSectorServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers() override;
        void update() override;

        [[nodiscard]] bool isWorldMode() const override { return worldMode; }

        bool createWorld(const std::string& name, const std::string& filePath,
                         const world::SectorConfig& sectorConfig,
                         const world::SectorStreamingConfig& streamingConfig);
        bool saveWorld(const std::string& filePath);
        bool loadWorld(const std::string& filePath);

        void clearWorld();

        bool saveSector(uint8_t gridIndex, const world::SectorCoord& coord, const std::string& filePath);
        bool loadSector(uint8_t gridIndex, const world::SectorCoord& coord);
        bool unloadSector(uint8_t gridIndex, const world::SectorCoord& coord);

        // ---- VK-1599: grid management ----------------------------------------------------

        // Appends a grid and its runtime. Returns the new index, or kMaxGrids if the world is
        // already at the cap. The grid starts empty - entities move into it by having their
        // StreamingPolicyComponent::gridIndex set.
        uint8_t addGrid(const std::string& name, const world::SectorConfig& sectorConfig,
                        const world::SectorStreamingConfig& streamingConfig);

        // Removes a grid, unloading its sectors first. Refuses on the primary grid (it is what
        // terrain, ocean, navmesh and HLOD are bound to) and on a grid that still owns sector
        // files - those must be migrated or deleted deliberately, never dropped as a side effect.
        // The refusal message is returned; empty means it happened.
        std::string removeGrid(uint8_t gridIndex);

        // ---- VK-1598: re-partition / cell-size migration (WorldSectorRepartitionOps.cpp) ----

        // Dry run: does everything applyRepartition does up to, but not including, writing a byte.
        [[nodiscard]] world::RepartitionSummary previewRepartition(uint8_t gridIndex,
                                                                   const world::SectorConfig& newConfig);

        // Destructive. Save World -> read every .vfsector -> re-bucket -> temp-write -> swap, with
        // the outgoing set kept as sectors.bak/ and <world>.vfworld.bak. Reloads the world.
        // Repartitions exactly one grid; the others' sector files are untouched.
        bool applyRepartition(uint8_t gridIndex, const world::SectorConfig& newConfig,
                              world::RepartitionSummary* outSummary = nullptr);

        // The inverse of the creation wizard. Leaves every entity live in the scene graph and the
        // world closed; the scene is UNSAVED afterwards.
        bool convertWorldToFlat();

        // VK-1599: the primary grid's manager. Kept as the no-argument spelling because every
        // caller of this accessor means "the world's main grid".
        world::WorldSectorManager& getSectorManager() { return primaryRuntime().manager; }
        world::WorldSectorManager& getSectorManager(uint8_t gridIndex) { return gridRuntime(gridIndex).manager; }
        const world::WorldDefinition& getWorldDefinition() const { return worldDefinition; }

    private:
        struct AsyncSectorLoadResult
        {
            std::vector<nlohmann::json> entityData;
            world::SectorDataLayers dataLayers;
            // VK-1591: raw .vfsector bytes when this job was a PREFETCH (read only, no parse).
            // entityData/dataLayers stay empty in that case. The flag lives on the RESULT rather
            // than being inferred from sector state, because the sector may have been promoted
            // Prefetching -> Loading while the read was still in flight.
            std::vector<uint8_t> prefetchBytes;
            bool wasPrefetch = false;
            bool success = false;
            // VK-1592: the ResourceLoadScheduler dropped this request before executeLoad ever
            // ran - cancelled by us, evicted by a full queue, or caught in scheduler shutdown.
            // Distinct from success == false, which means the read/parse genuinely failed: an
            // abandoned load is not an error and the streamer simply re-emits next frame.
            //
            // Defaults to TRUE on purpose. ~AsyncResultSlot resolves a dropped request with a
            // default-constructed result, and that is exactly the "never ran" case; submitSectorLoad
            // clears the flag on anything that actually came back from the work lambda.
            bool abandoned = true;
        };

        // VK-1592: sector reads are LoadRequests on the shared ResourceLoadScheduler rather than
        // bare std::async, so the streamer decides WHAT to load and in what order while the
        // scheduler decides WHEN and how many. AsyncResultSlot bridges the scheduler's "I own the
        // worker" model back to the future AsyncLoadQueue polls.
        //
        // The slot pointer here MUST be weak: the only strong reference lives inside the request's
        // executeLoad lambda, so every way the scheduler can drop a request destroys that lambda
        // and lets ~AsyncResultSlot resolve the future. A shared_ptr here would pin the slot,
        // strand the future and hang drainSectorLoads() forever.
        using SectorLoadSlot = streaming::AsyncResultSlot<AsyncSectorLoadResult>;
        struct PendingSectorRequest
        {
            std::weak_ptr<SectorLoadSlot> slot;
            resource::CancellationToken::Ptr cancellation;
            // Bytes charged against maxPrefetchBytes while this read is in flight (0 for
            // activations), refunded by releaseSectorRequest.
            uint64_t prefetchReservation = 0;
        };

        // VK-1599: everything that is per-GRID rather than per-world.
        //
        // The ticket framed this as "N manager/streamer instances"; that understates it. Every
        // container below is keyed on a bare SectorCoord, so with two grids they would collide on
        // exactly the same coords that sectorCoordToId does - a grid-1 sector at (0,0) would find
        // the grid-0 blob, cancel the grid-0 request and resolve the grid-0 future. Bundling them
        // per grid keeps every SectorCoord key correct without inventing a composite key type.
        struct GridRuntime
        {
            world::WorldSectorManager manager;
            world::SectorStreamer streamer;

            streaming::AsyncLoadQueue<world::SectorCoord, AsyncSectorLoadResult,
                                      world::SectorCoordHash> pendingAsyncLoads;
            std::unordered_map<world::SectorCoord, PendingSectorRequest,
                               world::SectorCoordHash> sectorRequests;

            // VK-1591: raw .vfsector bytes for sectors in target state Prefetched. Bytes rather
            // than the parsed DOM: exact-size accounting and ~5-10x smaller than
            // vector<nlohmann::json>, which directly bounds the unbounded-prefetch-memory risk.
            // Lives here rather than on world::WorldSector because it is service-private lifecycle
            // state - a WorldSector is a value type that plugins copy and inspect through sdk/.
            std::unordered_map<world::SectorCoord, std::vector<uint8_t>,
                               world::SectorCoordHash> prefetchedBlobs;
            uint64_t prefetchedBytes = 0; // running sum of prefetchedBlobs value sizes
            // VK-1592: prefetch reads no longer have an in-flight count cap (the scheduler governs
            // concurrency now), so maxPrefetchBytes must also account for reads still in flight or
            // a burst would overshoot the cap by (in-flight x sector size) before any of it lands.
            uint64_t prefetchBytesInFlight = 0;

            // VK-1595: a session-only streaming config for tuning radii and budgets without
            // dirtying the world. NEVER serialized - saveWorld writes the grid's persisted config,
            // which this deliberately shadows rather than replaces. Retained across play/stop so a
            // tuning session survives a play test; cleared when the world closes.
            std::optional<world::SectorStreamingConfig> streamingConfigOverride;
        };

        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        // VK-1599: never empty, and held by pointer rather than by value. The editor can add a grid
        // to an open world, and a vector reallocation would move every GridRuntime while sector
        // reads are in flight - the executeLoad lambdas and AsyncResultSlot chain hold references
        // that must stay put. Indirection buys stable addresses for at most world::kMaxGrids grids.
        std::vector<std::unique_ptr<GridRuntime>> grids;

        world::WorldDefinition worldDefinition;
        world::SectorEntityLoader entityLoader;
        world::PendingReferenceResolver referenceResolver;
        world::HLODStreamer hlodStreamer;
        world::HLODProxyManager hlodProxyManager;
        HLODWorldBaker hlodBaker;

        // Atomic because the notification handlers that gate on it (entity-deleted,
        // transform-changed, and the VK-1589 streaming-source commands) are published from the
        // "Scripts" frame task, which runs on an enkiTS worker, while every write happens on
        // the main thread. Every use is a plain load or store, so the implicit conversions
        // keep the call sites unchanged.
        std::atomic<bool> worldMode = false;
        bool isPlayMode = false;
        bool debugDrawSectors = false;
        std::string currentWorldPath;

        // VK-1595: in-viewport overlay visibility. Session-only editor state, same shape as
        // debugDrawSectors above - the toggle lives in the World Sectors window while the drawing
        // lives in the viewport, and the service is the only thing both can reach.
        bool streamingOverlayVisible = false;

        // VK-1595: the position the streamer actually used as source 0 on the last live frame.
        // The overlay centres on this rather than re-deriving a camera position, so the panel can
        // never disagree with the rings it is drawing.
        glm::vec3 lastStreamingOrigin{0.0f};

        // VK-1595: reused across frames so the overlay path allocates nothing in the steady state.
        // clear() keeps capacity; the snapshot hands out borrowed pointers into these.
        std::vector<::events::world::StreamingOverlayCell> overlayCells;
        std::vector<::events::world::StreamingOverlaySource> overlaySources;

        // VK-1599: each streamer's raw output, and the merged execution order over all of them.
        // Members rather than locals so the steady state costs no per-frame allocation, matching
        // hlodActions below. perGridActions keeps its per-grid capacity across frames.
        std::vector<std::vector<world::SectorStreamingAction>> perGridActions;
        std::vector<world::GridStreamingAction> streamingActions;
        std::vector<world::HLODStreamingAction> hlodActions;

        // VK-1594: proxies whose fade-out completed this frame, drained right after
        // HLODProxyManager::update. A member rather than a local so the steady state costs no
        // per-frame allocation, matching hlodActions above.
        std::vector<world::HLODCellCoord> expiredHlodCells;

        // VK-1590: targets that were already resident when their referencing entity spawned.
        // Batched here rather than probed per-entity so a frame's whole spawn budget costs one
        // resolver pass, not one per entity. Main-thread only, like the rest of update().
        std::vector<uint64_t> refTargetProbeQueue;

        // Multiple streaming sources (camera + gameplay-registered sources)
        std::unordered_map<uint32_t, world::StreamingSource> streamingSources;
        // sourceId -> owning entity UUID, for auto-unregister on entity deletion
        std::unordered_map<uint32_t, uint64_t> streamingSourceOwners;
        uint32_t nextStreamingSourceId = 1;
        // VK-1589: the three fields above are the only state this class shares across threads.
        // update() is pinned to the main thread, but the "Scripts" frame task is not and has no
        // dependency edge to "WorldSector" (EditorFrameTaskGraph.cpp), so a script calling
        // Streaming::registerWorldSource can rehash streamingSources while update() iterates it.
        // Hold this only around the map accesses - never across streamer.update().
        mutable std::mutex streamingSourcesMutex;

        ::events::SubscriptionToken transformChangedToken;
        ::events::SubscriptionToken editorModeChangedToken;
        ::events::SubscriptionToken cameraPositionToken;
        ::events::SubscriptionToken entityDeletedToken;

        ::events::SubscriptionToken sceneLoadedToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken entityCreatedToken;
        ::events::SubscriptionToken terrainCreatedToken;
        ::events::SubscriptionToken terrainLoadedToken;
        ::events::SubscriptionToken streamingPolicyChangedToken; // VK-1597
        ::events::SubscriptionToken sceneSavedToken;             // VK-1597

        // VK-1597: entities pulled out of a sector because they are not spatially loaded, counted
        // since the last scene save. They now persist ONLY in the .vfscene, and Save World is not
        // what writes that - so the World Sectors window uses this to tell the user a Save Scene is
        // still owed. Reset by SceneSavedNotification, deliberately NOT by saveWorld.
        uint32_t alwaysLoadedMigrationCount = 0;

        glm::vec3 cachedCameraPos{0.0f};
        std::chrono::steady_clock::time_point lastUpdateTime = std::chrono::steady_clock::now();

        // VK-1593: last frame's position per streaming-source id, purely to derive velocity.
        // Rebuilt into the scratch map each frame and swapped, so a source that unregisters drops
        // out on its own. Main-thread only (update()), unlike streamingSources above - it is
        // never touched from the "Scripts" task, so it needs no lock.
        std::unordered_map<uint32_t, glm::vec3> lastSourcePositionsForVelocity;
        std::unordered_map<uint32_t, glm::vec3> sourceVelocityScratch;

        // Saved state for play/stop transitions
        world::WorldDefinition savedWorldDefinition;
        std::string savedWorldPath;

        struct AsyncHlodLoadResult
        {
            world::HLODFileData data;
            bool success = false;
            bool abandoned = true; // see AsyncSectorLoadResult::abandoned
        };
        using HlodLoadSlot = streaming::AsyncResultSlot<AsyncHlodLoadResult>;
        struct PendingHlodRequest
        {
            std::weak_ptr<HlodLoadSlot> slot;
            resource::CancellationToken::Ptr cancellation;
        };
        streaming::AsyncLoadQueue<world::HLODCellCoord, AsyncHlodLoadResult,
                                  world::HLODCellCoordHash> pendingHlodLoads;
        std::unordered_map<world::HLODCellCoord, PendingHlodRequest,
                           world::HLODCellCoordHash> hlodRequests;

        // Physics state snapshots for velocity/sleep preservation across sector streaming
        struct PhysicsSnapshot
        {
            glm::vec3 linearVelocity{0.0f};
            glm::vec3 angularVelocity{0.0f};
            bool wasSleeping = false;
        };
        std::unordered_map<uint64_t, PhysicsSnapshot> physicsSnapshots; // keyed by entity UUID

        // Animation state snapshots for state preservation across sector streaming
        std::unordered_map<uint64_t, ::events::animation::snapshot::AnimationSnapshot> animationSnapshots;

        // VFX playback snapshots for state preservation across sector streaming
        struct VFXSnapshot
        {
            float emissionTime = 0.0f;
            float spawnAccumulator = 0.0f;
            bool wasPlaying = true;
            bool wasActive = true;
        };
        std::unordered_map<uint64_t, VFXSnapshot> vfxSnapshots; // keyed by entity UUID

        // Audio playback snapshots for state preservation across sector streaming
        struct AudioSnapshot
        {
            bool wasPlaying = false;
            float playbackPosition = 0.0f;
            float volume = 1.0f;
            float pitch = 1.0f;
            bool loop = false;
            bool is3D = false;
            std::string audioPath;
            std::string busName;
        };
        std::unordered_map<uint64_t, AudioSnapshot> audioSnapshots; // keyed by entity UUID

        // ---- VK-1599 grid runtime access -------------------------------------------------
        //
        // `grids` is never empty once the service is constructed, and every accessor clamps: a
        // gridIndex can reach here from a StreamingPolicyComponent authored against a world with
        // more grids than the one now open, and falling back to the primary grid is the
        // non-destructive reading of that.
        [[nodiscard]] uint8_t gridCount() const { return static_cast<uint8_t>(grids.size()); }
        [[nodiscard]] GridRuntime& gridRuntime(uint8_t gridIndex)
        {
            return *grids[gridIndex < grids.size() ? gridIndex : world::kPrimaryGridIndex];
        }
        [[nodiscard]] const GridRuntime& gridRuntime(uint8_t gridIndex) const
        {
            return *grids[gridIndex < grids.size() ? gridIndex : world::kPrimaryGridIndex];
        }
        [[nodiscard]] GridRuntime& primaryRuntime() { return *grids[world::kPrimaryGridIndex]; }
        [[nodiscard]] const GridRuntime& primaryRuntime() const { return *grids[world::kPrimaryGridIndex]; }

        // Rebuild the runtime list to match worldDefinition.grids, preserving nothing. Every path
        // that opens or closes a world goes through it, so a GridRuntime never outlives the grid
        // definition it was built for.
        void rebuildGridRuntimes();

        // The grid whose manager currently buckets this entity, if any. Used by every path that
        // starts from a UUID rather than from a grid - transform changes, selection, entity
        // deletion. Returns kMaxGrids when no grid knows the entity.
        [[nodiscard]] uint8_t findEntityGrid(uint64_t uuid) const;

        // VK-1599: where an entity BELONGS, as opposed to where it currently sits. Reads the grid
        // off its StreamingPolicyComponent and resolves the coord against that grid's own sector
        // size. The single entry point for the five engine sites that used to call
        // world::resolveEntitySectorAssignment with the one sector config there was.
        [[nodiscard]] world::SectorAssignment resolveEntityAssignment(const scene::Entity& entity) const;

        // The per-grid sector configs, in grid order, for the pure resolver in SectorAssignment.
        // Rebuilt on demand rather than cached: it is at most kMaxGrids small PODs, and a cache
        // would need invalidating from every path that edits a grid's config.
        [[nodiscard]] std::vector<world::SectorConfig> gridSectorConfigs() const;

        void handleSectorLoad(uint8_t gridIndex, const world::SectorCoord& coord);
        void handleSectorUnload(uint8_t gridIndex, const world::SectorCoord& coord);

        // VK-1592 scheduler plumbing. submitSectorLoad tracks the future in the grid's
        // pendingAsyncLoads BEFORE handing the work to the scheduler, and returns false when a load
        // for this coord is already tracked - callers that have already begun activation must
        // rewind.
        [[nodiscard]] bool submitSectorLoad(uint8_t gridIndex,
                                            const world::SectorCoord& coord,
                                            resource::LoadImportance importance,
                                            float hintPriority,
                                            uint64_t estimatedBytes,
                                            uint64_t prefetchReservation,
                                            std::string debugName,
                                            std::function<AsyncSectorLoadResult()> work);
        void releaseSectorRequest(uint8_t gridIndex, const world::SectorCoord& coord);
        void cancelSectorRequest(uint8_t gridIndex, const world::SectorCoord& coord);
        // Drains EVERY grid - it is only ever called when the whole world is changing underneath.
        void drainSectorLoads();
        [[nodiscard]] glm::vec3 sectorCenterWorld(uint8_t gridIndex, const world::SectorCoord& coord) const;
        [[nodiscard]] resource::LoadHint sectorLoadHint(uint8_t gridIndex,
                                                        const world::SectorCoord& coord,
                                                        resource::LoadImportance importance,
                                                        float hintPriority) const;

        void submitHlodLoad(const world::HLODCellCoord& cell, const std::string& filePath,
                            const glm::vec3& cellCenter);

        // VK-1594: destroys the proxy entities and frees its GPU geometry. Always use this rather
        // than a bare hlodProxyManager.unloadProxy - in-memory HLOD meshes are pinned and are
        // never reclaimed by the renderer's ordinary eviction sweep.
        void releaseHLODProxy(const world::HLODCellCoord& cell);
        void cancelHlodRequest(const world::HLODCellCoord& cell);
        void pollAsyncHlodLoads();
        void drainHlodLoads();

        // VK-1591 prefetch ring. beginSectorActivation publishes the two activation
        // notifications and flips the sector to Loading; the prefetch path deliberately
        // publishes nothing until that point.
        void beginSectorActivation(uint8_t gridIndex, world::WorldSector& sector);
        void handleSectorPrefetch(uint8_t gridIndex, const world::SectorCoord& coord);
        void handleSectorPrefetchDrop(uint8_t gridIndex, const world::SectorCoord& coord);
        void launchParseFromCachedBlob(uint8_t gridIndex, const world::SectorCoord& coord);
        void dropPrefetchedBlob(uint8_t gridIndex, const world::SectorCoord& coord);
        // Clears EVERY grid's blob cache, for the same reason drainSectorLoads drains them all.
        void clearPrefetchedBlobs();
        void invalidateHLODForSector(const world::SectorCoord& coord);
        bool generateSectorHLOD(const world::SectorCoord& coord, uint8_t tier);
        void processHLODRegenQueue();

        // VK-1594: look a tier up by its id rather than indexing hlodConfig.tiers positionally -
        // the tier table comes from .vfworld verbatim, so index and tier id need not agree.
        [[nodiscard]] const world::HLODTierConfig* findHLODTier(uint8_t tier) const;

        // VK-1594: the cell's baked .vfHLOD. Prefers the per-cell hlodCells inventory; for tier 0
        // it falls back to WorldSector::hlodFilePath so worlds baked before hlodCells existed
        // still resolve. Empty when the cell has no bake.
        [[nodiscard]] std::string resolveHLODCellPath(const world::HLODCellCoord& cell,
                                                      const world::HLODTierConfig& tierConfig) const;

        // Directory the .vfworld lives in; every mesh path inside a .vfsector is relative to it.
        [[nodiscard]] std::string hlodWorkingDirectory() const;

        // VK-1594: snapshot the per-tier cell work list and hand it to the async baker. Returns
        // whether a bake was started (false if one is already running or nothing needs baking).
        bool beginHLODBake(bool missingOnly);

        // VK-1594: bake one cell at any tier, synchronously. Tier 0 routes to generateForSector to
        // preserve the historical per-sector output; tiers 1+ merge their member sectors through
        // the previously dead generateForCell.
        bool bakeHLODCell(const world::HLODCellCoord& cell);

        // Main-thread bookkeeping for a finished bake. Split out from bakeHLODCell because the
        // async baker runs the generation on a worker and applies the result back here.
        void recordHLODBake(const world::HLODCellCoord& cell, const std::string& outputPath);

        // Cells whose HLOD was invalidated, awaiting automatic re-bake
        // (drained one per frame in edit mode while streaming is idle).
        // VK-1594: keyed by cell, not sector, so tier 1/2 re-bakes can be queued too.
        std::deque<world::HLODCellCoord> hlodRegenQueue;
        void pollAsyncSectorLoads();
        void finalizeSectorLoad(uint8_t gridIndex, const world::SectorCoord& coord,
                                std::vector<nlohmann::json>& entityData,
                                world::SectorDataLayers& dataLayers);
        void onTransformChanged(uint64_t uuid, const glm::vec3& newPosition);

        // VK-1597: interactive half of the streaming-policy flip - takes effect immediately so the
        // entity survives its former sector's very next unload, without waiting for a save.
        void onStreamingPolicyChanged(uint64_t uuid, bool spatiallyLoaded);

        // VK-1597: authoritative half. The component can also arrive through routes that publish
        // nothing (prefab instantiation, a script, undo), and EntityCreatedNotification fires on a
        // BARE entity before any component is deserialized onto it - so the notification path alone
        // would leave such an entity bucketed forever. Run at the top of saveWorld, which is the
        // ticket's own "migrates it out on next Save World". Returns how many entities moved.
        uint32_t reconcileAlwaysLoadedEntities();

        void onTerrainAvailable(float worldTileSize);
        // VK-1593: position plus the unit look direction of the play-mode camera. Replaces the
        // position-only getter - every caller wanted the pose. `forward` is left zero when there
        // is no primary camera, which SectorStreamer reads as "omni".
        struct CameraPose
        {
            glm::vec3 position{0.0f};
            glm::vec3 forward{0.0f};
        };
        CameraPose getPrimaryCameraPose() const;
        // VK-1593: fills StreamingSource::velocity for every source from its own position delta
        // over deltaTime. Deliberately unguarded - SectorStreamer owns the teleport guard and
        // discards the velocity on any frame it classifies as a jump, so there is exactly one
        // place in the engine that decides jump-vs-motion.
        void updateStreamingSourceVelocities(std::vector<world::StreamingSource>& sources,
                                             float deltaTime);
        void drawDebugSectors() const;

        // VK-1595: the ONLY correct way to read a grid's streaming config at runtime - a session
        // override shadows the grid's persisted one. Every former direct read of
        // worldDefinition.streamingConfig goes through here; the persisted member is now touched
        // only by the persistence path and by SetStreamingConfigCommand.
        //
        // VK-1599: the override is per grid, so tuning the clutter grid's radii cannot disturb the
        // landmark grid's.
        [[nodiscard]] const world::SectorStreamingConfig& getEffectiveStreamingConfig(uint8_t gridIndex) const
        {
            const uint8_t index = worldDefinition.clampGridIndex(gridIndex);
            return world::effectiveStreamingConfig(gridRuntime(index).streamingConfigOverride,
                                                   worldDefinition.grid(index).streamingConfig);
        }

        // Push each grid's effective config into its streamer, and the primary grid's into the HLOD
        // streamer. Call after ANY change to an override or to a persisted config, or a streamer
        // keeps running a stale one.
        void applyEffectiveStreamingConfig();

        // VK-1595: clear the session override and unfreeze. Called from every path that closes a
        // world - a tuning override belongs to the world it was tuned against, and a streamer left
        // paused across a world change reads as "streaming is broken".
        void resetStreamingSessionState();

        // VK-1595: refill the overlay buffers and return borrowed views over them.
        // VK-1599: one grid per call - the buffers are shared and the snapshot is documented
        // single-consumer, so returning every grid at once is exactly the aliasing hazard that
        // contract forbids. The panel switches grids instead.
        [[nodiscard]] ::events::world::StreamingOverlaySnapshot buildOverlaySnapshot(uint8_t gridIndex,
                                                                                     int32_t maxRadius);

        // Drop every gameplay-registered streaming source and restart id allocation.
        // Takes streamingSourcesMutex - do not call while already holding it.
        void clearStreamingSources();

        // VK-1590: one-time full-registry sweep, for references held by entities that already
        // exist when world mode is entered (the main scene). Those never pass through the
        // sector spawn path, so PostLoad registration alone would miss them. Idempotent.
        void rescanEntityReferences();

        // ---- VK-1598 internals (WorldSectorRepartitionOps.cpp) ----

        // The guards both repartition entry points share, phrased as a refusal message: empty
        // means "go ahead". `newConfig` is null for convert-to-flat, which has no target config.
        [[nodiscard]] std::string repartitionRefusal(uint8_t gridIndex,
                                                     const world::SectorConfig* newConfig) const;

        // Reads every .vfsector named by this grid's sectorFilePaths. Returns false on the first
        // unreadable file - a partial read would silently drop a sector's entities.
        [[nodiscard]] bool readAllSourceSectors(uint8_t gridIndex,
                                                std::vector<world::RepartitionSourceSector>& out) const;

        // Destroys every entity any grid's sector manager knows about. Needed before reloading a
        // repartitioned world: clearWorld leaves streamed entities alive in the scene, and
        // SectorEntityLoader skips any UUID already in the registry - so without this the reloaded
        // sectors come back empty and the entities are orphaned at their old coords.
        void destroyAllSectorEntities();

        // Moves this world's outgoing files - every sector named by `paths`, each one's tier-0
        // .vfHLOD sibling, and every tier-1+ bake in `cells` - into `backupDir`. Deliberately
        // per-file rather than renaming the sectors/ directory: a second .vfworld in the same
        // folder shares that directory, and taking it wholesale would carry off its sectors too.
        [[nodiscard]] bool moveWorldFilesToBackup(
            const std::unordered_map<world::SectorCoord, std::string, world::SectorCoordHash>& paths,
            const std::unordered_map<world::HLODCellCoord, std::string,
                                     world::HLODCellCoordHash>& cells,
            const std::filesystem::path& backupDir) const;
    };

} // namespace services
