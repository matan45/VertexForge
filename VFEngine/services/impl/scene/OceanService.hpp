#pragma once

#include "../../interfaces/terrain/IOceanService.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/OceanData.hpp"
#include "../../events/terrain/OceanEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../../utilities/world/WorldTypes.hpp"
#include "../../../utilities/terrain/TerrainTypes.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

namespace scene
{
    class SceneGraphSystem;
}

namespace water
{
    class WaterTileGrid;
}

namespace services
{
    class IPhysicsProvider;

    class OceanService : public IOceanService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        IPhysicsProvider* physicsProvider = nullptr;

        EntityHandle oceanEntity;

        OceanFFTConfigData oceanConfig;
        uint32_t oceanConfigVersion = 0;

        std::function<float(const glm::vec2&)> oceanHeightSampler;

        std::unordered_set<EntityHandle, EntityHandle::Hash> entitiesInWater;

        std::unique_ptr<::events::SubscriptionToken> entityDeletedSubscription;
        std::unique_ptr<::events::SubscriptionToken> sceneClearedSubscription;

        // Sector-driven water tile streaming (world mode)
        bool worldModeActive = false;
        world::SectorConfig cachedSectorConfig;
        std::unique_ptr<water::WaterTileGrid> waterTileGrid;
        std::unique_ptr<::events::SubscriptionToken> sectorActivatedSub;
        std::unique_ptr<::events::SubscriptionToken> sectorDeactivatedSub;
        std::unique_ptr<::events::SubscriptionToken> worldLoadedSub;

        struct PendingWaterTileAction
        {
            terrain::TileCoord coord;
            bool isLoad;
        };
        std::mutex pendingActionsMutex;
        std::vector<PendingWaterTileAction> pendingSectorTileActions;

    public:
        explicit OceanService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~OceanService() override;

        void setPhysicsProvider(IPhysicsProvider* provider) { physicsProvider = provider; }
        IPhysicsProvider* getPhysicsProvider() const { return physicsProvider; }
        void setOceanHeightSampler(std::function<float(const glm::vec2&)> sampler) { oceanHeightSampler = std::move(sampler); }

        void registerEventHandlers() override;

        EntityHandle createOcean(const OceanCreationData& config) override;
        bool deleteOcean(EntityHandle oceanEntity) override;
        std::optional<OceanData> getOceanData(EntityHandle entity) const override;
        bool hasOceanComponent(EntityHandle entity) const override;

        bool hasActiveOcean() const { return oceanEntity.isValid(); }

        bool isPositionInOcean(const glm::vec3& worldPos) const;
        float getOceanHeightAt(const glm::vec2& worldXZ) const;

        OceanVisualSettings getOceanVisualSettings() const;
        float getBaseWaterHeight() const;

        bool isOceanFFTEnabled() const { return oceanConfig.enabled; }
        OceanFFTConfigData getOceanFFTConfig() const { return oceanConfig; }
        uint32_t getOceanFFTConfigVersion() const { return oceanConfigVersion; }

        bool saveOcean(EntityHandle oceanEntity, const std::string& path);
        EntityHandle loadOcean(const std::string& path);

        void updateBuoyancy();
        void clearBuoyancyTracking();

        void rebuildOceanFromComponents();

        // Water tile streaming
        bool isWorldModeActive() const { return worldModeActive; }
        const water::WaterTileGrid* getWaterTileGrid() const;
        void processPendingSectorTileActions();

    private:
        void registerOceanCoreHandlers(::events::EventDispatcher& dispatcher);
        void registerOceanQueryHandlers(::events::EventDispatcher& dispatcher);
        void registerOceanFFTHandlers(::events::EventDispatcher& dispatcher);

        void onEntityDeleted(EntityHandle entity);
        void onSceneCleared();

        // Sector-driven water tile streaming
        void onSectorActivated(const world::SectorCoord& coord, const world::SectorConfig& config);
        void onSectorDeactivated(const world::SectorCoord& coord, const world::SectorConfig& config);
        void activateWaterTilesForLoadedSectors();
    };
}
