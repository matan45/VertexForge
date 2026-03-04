#pragma once

#include "../../interfaces/terrain/IWaterService.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../events/terrain/WaterEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "water/WaterTypes.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace scene
{
    class SceneGraphSystem;
}

namespace water
{
    class WaterGrid;
    struct WaterTile;
}

namespace services
{
    class IPhysicsProvider;
}

namespace services
{
    class WaterService : public IWaterService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        IPhysicsProvider* physicsProvider = nullptr;

        std::unordered_map<uint64_t, std::unique_ptr<water::WaterGrid>> waterGrids;
        std::unordered_set<EntityHandle, EntityHandle::Hash> entitiesInWater;

        mutable water::WaterGlobalSettings cachedGlobalSettings;
        mutable bool globalSettingsDirty = true;

        bool distanceCullingEnabled = false;
        float maxWaterDistSq = 0.0f;

        std::unique_ptr<::events::SubscriptionToken> entityDeletedSubscription;
        std::unique_ptr<::events::SubscriptionToken> sceneClearedSubscription;
        std::unique_ptr<::events::SubscriptionToken> triggerEnterSubscription;
        std::unique_ptr<::events::SubscriptionToken> triggerExitSubscription;


    public:
        explicit WaterService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~WaterService() override;

        void setPhysicsProvider(IPhysicsProvider* provider) { physicsProvider = provider; }

        void registerEventHandlers() override;

        EntityHandle createWater(const WaterCreationData& config) override;
        bool deleteWater(EntityHandle waterEntity) override;
        std::optional<WaterData> getWaterData(EntityHandle entity) const override;
        bool hasWaterComponent(EntityHandle entity) const override;

        bool hasWaterTileComponent(EntityHandle entity) const;

        std::vector<water::WaterTile*> getVisibleWaterTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition);

        std::vector<water::WaterTile*> queryVisibleWaterTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition);

        bool hasActiveWater() const { return !waterGrids.empty(); }

        bool isPositionInWater(const glm::vec3& worldPos) const;
        float getWaterHeightAt(const glm::vec2& worldXZ) const;

        water::WaterGlobalSettings getWaterGlobalSettings() const;
        water::WaterTileConfig getWaterTileConfig() const;

        void setDistanceCullingEnabled(bool enabled) { distanceCullingEnabled = enabled; }
        void setMaxDrawDistance(float distance) { maxWaterDistSq = distance * distance; }

        void setWaterTileHeight(EntityHandle waterEntity, int32_t tileX, int32_t tileZ, float height);
        void setWaterGlobalSettings(EntityHandle waterEntity, const WaterGlobalSettingsData& settings);

        void updateBuoyancy();
        void clearBuoyancyTracking();

        void rebuildWaterFromComponents();
        void remapWaterEntities();

    private:
        void registerWaterCoreHandlers(::events::EventDispatcher& dispatcher);
        void registerWaterQueryHandlers(::events::EventDispatcher& dispatcher);

        void createTileEntities(EntityHandle parentEntity, water::WaterGrid& grid);
        void onEntityDeleted(EntityHandle entity);
        void onSceneCleared();
    };
}
