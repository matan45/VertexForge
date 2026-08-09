#pragma once

#include "../../interfaces/world/IWorldSectorService.hpp"
#include "../../events/EventTypes.hpp"
#include "../../events/animation/AnimationSnapshotEvents.hpp"
#include "world/WorldSectorManager.hpp"
#include "world/WorldDefinition.hpp"
#include "world/SectorStreamer.hpp"
#include "world/SectorEntityLoader.hpp"
#include "world/PendingReferenceResolver.hpp"
#include "world/HLODStreamer.hpp"
#include "world/HLODProxyManager.hpp"
#include "streaming/AsyncLoadQueue.hpp"
#include <atomic>
#include <chrono>
#include <deque>
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
            bool success = false;
        };

        streaming::AsyncLoadQueue<world::SectorCoord, AsyncSectorLoadResult,
                                  world::SectorCoordHash> pendingAsyncLoads;

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
    };

} // namespace services
