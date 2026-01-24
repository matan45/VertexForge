#pragma once
#include "scene/LightBVH.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include <entt/entt.hpp>

namespace controllers::offscreen
{
    class LightBVHManager
    {
    private:
        scene::LightBVH lightBVH;

        events::ScopedSubscription lightDataChangedSubscription;
        events::ScopedSubscription lightComponentChangedSubscription;
        events::ScopedSubscription entityDeletedSubscription;
        events::ScopedSubscription entityStaticChangedSubscription;
        events::ScopedSubscription transformChangedSubscription;
        events::ScopedSubscription sceneLoadedSubscription;
        events::ScopedSubscription sceneClearedSubscription;
        events::ScopedSubscription prefabInstantiatedSubscription;
        events::ScopedSubscription entityDuplicatedSubscription;

        static bool hasAnyLightComponent(entt::entity entity);

    public:
        explicit LightBVHManager();
        ~LightBVHManager() = default;

        void init();
        void update();

        size_t getStaticLightCount() const { return lightBVH.getStaticLightCount(); }
        size_t getDynamicLightCount() const { return lightBVH.getDynamicLightCount(); }
        size_t getStaticNodeCount() const { return lightBVH.getStaticNodeCount(); }
        size_t getDynamicNodeCount() const { return lightBVH.getDynamicNodeCount(); }
    };
}
