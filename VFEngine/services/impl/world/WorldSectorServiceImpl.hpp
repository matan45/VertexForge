#pragma once

#include "../../interfaces/world/IWorldSectorService.hpp"
#include "../../events/EventTypes.hpp"
#include "../../events/animation/AnimationSnapshotEvents.hpp"
#include "world/WorldSectorManager.hpp"
#include "world/WorldDefinition.hpp"
#include "world/SectorStreamer.hpp"
#include "world/SectorEntityLoader.hpp"
#include "world/PendingReferenceResolver.hpp"
#include <future>
#include <memory>
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

        bool worldMode = false;
        bool isPlayMode = false;
        bool debugDrawSectors = false;
        std::string currentWorldPath;

        std::vector<world::SectorStreamingAction> streamingActions;

        ::events::SubscriptionToken transformChangedToken;
        ::events::SubscriptionToken editorModeChangedToken;
        ::events::SubscriptionToken cameraPositionToken;

        ::events::SubscriptionToken sceneLoadedToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken entityCreatedToken;
        ::events::SubscriptionToken terrainCreatedToken;
        ::events::SubscriptionToken terrainLoadedToken;

        glm::vec3 cachedCameraPos{0.0f};

        // Saved state for play/stop transitions
        world::WorldDefinition savedWorldDefinition;
        std::string savedWorldPath;

        struct AsyncSectorLoadResult
        {
            std::vector<nlohmann::json> entityData;
            bool success = false;
        };

        struct PendingAsyncSectorLoad
        {
            world::SectorCoord coord;
            std::future<AsyncSectorLoadResult> future;
            bool cancelled = false;
        };

        std::unordered_map<world::SectorCoord, PendingAsyncSectorLoad, world::SectorCoordHash> pendingAsyncLoads;

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

        void handleSectorLoad(const world::SectorCoord& coord);
        void handleSectorUnload(const world::SectorCoord& coord);
        void pollAsyncSectorLoads();
        void finalizeSectorLoad(const world::SectorCoord& coord, std::vector<nlohmann::json>& entityData);
        void onTransformChanged(uint64_t uuid, const glm::vec3& newPosition);
        void onTerrainAvailable(float worldTileSize);
        glm::vec3 getPrimaryCameraPosition() const;
        void drawDebugSectors() const;
    };

} // namespace services
