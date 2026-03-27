#pragma once

#include "../../interfaces/terrain/IOceanService.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/OceanData.hpp"
#include "../../events/terrain/OceanEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

namespace scene
{
    class SceneGraphSystem;
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

    private:
        void registerOceanCoreHandlers(::events::EventDispatcher& dispatcher);
        void registerOceanQueryHandlers(::events::EventDispatcher& dispatcher);
        void registerOceanFFTHandlers(::events::EventDispatcher& dispatcher);

        void onEntityDeleted(EntityHandle entity);
        void onSceneCleared();
    };
}
