#pragma once

#include "../../interfaces/world/IWorldSectorService.hpp"
#include "../../events/EventTypes.hpp"
#include "world/WorldSectorManager.hpp"
#include "world/WorldDefinition.hpp"
#include "world/SectorStreamer.hpp"
#include "world/SectorEntityLoader.hpp"
#include "world/PendingReferenceResolver.hpp"
#include <memory>
#include <optional>
#include <string>

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

        glm::vec3 cachedCameraPos{0.0f};

        // Saved state for play/stop transitions
        world::WorldDefinition savedWorldDefinition;
        std::string savedWorldPath;

        void handleSectorLoad(const world::SectorCoord& coord);
        void handleSectorUnload(const world::SectorCoord& coord);
        void onTransformChanged(uint64_t uuid, const glm::vec3& newPosition);
        glm::vec3 getPrimaryCameraPosition() const;
        void drawDebugSectors() const;
    };

} // namespace services
