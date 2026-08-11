#pragma once

#include "../../interfaces/world/IWorldSectorService.hpp"
#include "../../events/EventTypes.hpp"
#include "../../events/animation/AnimationSnapshotEvents.hpp"
#include "world/WorldSectorManager.hpp"
#include "world/WorldDefinition.hpp"
#include "world/SectorStreamer.hpp"
#include "world/SectorEntityLoader.hpp"
#include "world/PendingReferenceResolver.hpp"
#include "world/SectorRefFieldRegistry.hpp"
#include "world/HLODStreamer.hpp"
#include "world/HLODProxyManager.hpp"
#include "world/HLODSerialization.hpp"
#include "streaming/AsyncLoadQueue.hpp"
#include "streaming/AsyncResultSlot.hpp"
#include "resource/CancellationToken.hpp"
#include "resource/ResourceLoadTypes.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
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

        bool saveSector(const world::SectorCoord& coord, const std::string& filePath);
        bool loadSector(const world::SectorCoord& coord);
        bool unloadSector(const world::SectorCoord& coord);

        world::WorldSectorManager& getSectorManager() { return sectorManager; }
        const world::WorldDefinition& getWorldDefinition() const { return worldDefinition; }

    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        world::WorldSectorManager sectorManager;
        world::WorldDefinition worldDefinition;
        world::SectorStreamer streamer;
        world::SectorEntityLoader entityLoader;
        world::PendingReferenceResolver referenceResolver;
        world::HLODStreamer hlodStreamer;
        world::HLODProxyManager hlodProxyManager;

        // Atomic because the notification handlers that gate on it (entity-deleted,
        // transform-changed, and the VK-1589 streaming-source commands) are published from the
        // "Scripts" frame task, which runs on an enkiTS worker, while every write happens on
        // the main thread. Every use is a plain load or store, so the implicit conversions
        // keep the call sites unchanged.
        std::atomic<bool> worldMode = false;
        bool isPlayMode = false;
        bool debugDrawSectors = false;
        std::string currentWorldPath;

        std::vector<world::SectorStreamingAction> streamingActions;
        std::vector<world::HLODStreamingAction> hlodActions;

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

        glm::vec3 cachedCameraPos{0.0f};
        std::chrono::steady_clock::time_point lastUpdateTime = std::chrono::steady_clock::now();

        // Saved state for play/stop transitions
        world::WorldDefinition savedWorldDefinition;
        std::string savedWorldPath;

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

        streaming::AsyncLoadQueue<world::SectorCoord, AsyncSectorLoadResult,
                                  world::SectorCoordHash> pendingAsyncLoads;

        // VK-1592: sector and HLOD reads are LoadRequests on the shared ResourceLoadScheduler
        // rather than bare std::async, so the streamer decides WHAT to load and in what order
        // while the scheduler decides WHEN and how many. AsyncResultSlot bridges the scheduler's
        // "I own the worker" model back to the future AsyncLoadQueue polls.
        //
        // The slot pointers here MUST be weak: the only strong reference lives inside the
        // request's executeLoad lambda, so every way the scheduler can drop a request destroys
        // that lambda and lets ~AsyncResultSlot resolve the future. A shared_ptr here would pin
        // the slot, strand the future and hang drainSectorLoads() forever.
        using SectorLoadSlot = streaming::AsyncResultSlot<AsyncSectorLoadResult>;
        struct PendingSectorRequest
        {
            std::weak_ptr<SectorLoadSlot> slot;
            resource::CancellationToken::Ptr cancellation;
            // Bytes charged against maxPrefetchBytes while this read is in flight (0 for
            // activations), refunded by releaseSectorRequest.
            uint64_t prefetchReservation = 0;
        };
        std::unordered_map<world::SectorCoord, PendingSectorRequest,
                           world::SectorCoordHash> sectorRequests;

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

        // VK-1591: raw .vfsector bytes for sectors in target state Prefetched. Bytes rather than
        // the parsed DOM: exact-size accounting and ~5-10x smaller than vector<nlohmann::json>,
        // which directly bounds the unbounded-prefetch-memory risk. Lives here rather than on
        // world::WorldSector because it is service-private lifecycle state - a WorldSector is a
        // value type that plugins copy and inspect through sdk/.
        std::unordered_map<world::SectorCoord, std::vector<uint8_t>,
                           world::SectorCoordHash> prefetchedBlobs;
        uint64_t prefetchedBytes = 0; // running sum of prefetchedBlobs value sizes
        // VK-1592: prefetch reads no longer have an in-flight count cap (the scheduler governs
        // concurrency now), so maxPrefetchBytes must also account for reads still in flight or a
        // burst would overshoot the cap by (in-flight x sector size) before any of it lands.
        uint64_t prefetchBytesInFlight = 0;

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

        void handleSectorLoad(const world::SectorCoord& coord);
        void handleSectorUnload(const world::SectorCoord& coord);

        // VK-1592 scheduler plumbing. submitSectorLoad tracks the future in pendingAsyncLoads
        // BEFORE handing the work to the scheduler, and returns false when a load for this coord
        // is already tracked - callers that have already begun activation must rewind.
        [[nodiscard]] bool submitSectorLoad(const world::SectorCoord& coord,
                                            resource::LoadImportance importance,
                                            float hintPriority,
                                            uint64_t estimatedBytes,
                                            uint64_t prefetchReservation,
                                            std::string debugName,
                                            std::function<AsyncSectorLoadResult()> work);
        void releaseSectorRequest(const world::SectorCoord& coord);
        void cancelSectorRequest(const world::SectorCoord& coord);
        void drainSectorLoads();
        [[nodiscard]] glm::vec3 sectorCenterWorld(const world::SectorCoord& coord) const;
        [[nodiscard]] resource::LoadHint sectorLoadHint(const world::SectorCoord& coord,
                                                        resource::LoadImportance importance,
                                                        float hintPriority) const;

        void submitHlodLoad(const world::HLODCellCoord& cell, const std::string& filePath,
                            const glm::vec3& cellCenter);
        void cancelHlodRequest(const world::HLODCellCoord& cell);
        void pollAsyncHlodLoads();
        void drainHlodLoads();

        // VK-1591 prefetch ring. beginSectorActivation publishes the two activation
        // notifications and flips the sector to Loading; the prefetch path deliberately
        // publishes nothing until that point.
        void beginSectorActivation(world::WorldSector& sector);
        void handleSectorPrefetch(const world::SectorCoord& coord);
        void handleSectorPrefetchDrop(const world::SectorCoord& coord);
        void launchParseFromCachedBlob(const world::SectorCoord& coord);
        void dropPrefetchedBlob(const world::SectorCoord& coord);
        void clearPrefetchedBlobs();
        void invalidateHLODForSector(const world::SectorCoord& coord);
        bool generateSectorHLOD(const world::SectorCoord& coord, uint8_t tier);
        void processHLODRegenQueue();

        // Sectors whose HLOD was invalidated, awaiting automatic re-bake
        // (drained one per frame in edit mode while streaming is idle)
        std::deque<world::SectorCoord> hlodRegenQueue;
        void pollAsyncSectorLoads();
        void finalizeSectorLoad(const world::SectorCoord& coord,
                                std::vector<nlohmann::json>& entityData,
                                world::SectorDataLayers& dataLayers);
        void onTransformChanged(uint64_t uuid, const glm::vec3& newPosition);
        void onTerrainAvailable(float worldTileSize);
        glm::vec3 getPrimaryCameraPosition() const;
        void drawDebugSectors() const;

        // Drop every gameplay-registered streaming source and restart id allocation.
        // Takes streamingSourcesMutex - do not call while already holding it.
        void clearStreamingSources();

        // VK-1590: one-time full-registry sweep, for references held by entities that already
        // exist when world mode is entered (the main scene). Those never pass through the
        // sector spawn path, so PostLoad registration alone would miss them. Idempotent.
        void rescanEntityReferences();
    };

} // namespace services
