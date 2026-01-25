#pragma once
#include "scene/LightBVH.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "math/Frustum.hpp"
#include <entt/entt.hpp>
#include <vector>

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
        events::ScopedSubscription editorModeChangedSubscription;

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

        // Query lights visible in the camera frustum
        // Returns entity IDs of all lights (point, spot, directional) intersecting the frustum
        void queryFrustum(const math::Frustum& frustum, std::vector<uint32_t>& visibleLights) const
        {
            lightBVH.queryFrustum(frustum, visibleLights);
        }

        // Get direct access to the LightBVH (for advanced usage)
        const scene::LightBVH& getLightBVH() const { return lightBVH; }
    };
}
