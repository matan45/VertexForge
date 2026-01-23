#pragma once
#include "scene/LightBVH.hpp"
#include "math/Frustum.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include <vector>
#include <cstdint>

namespace controllers::offscreen
{
    // Manages the Light BVH lifecycle with event-driven updates
    // Follows the same pattern as SceneBVHManager but for light entities
    class LightBVHManager
    {
    private:
        scene::LightBVH lightBVH;

        // Event subscriptions (RAII - automatically unsubscribe on destruction)
        events::ScopedSubscription lightDataChangedSubscription;
        events::ScopedSubscription lightComponentChangedSubscription;  // Handles both add/remove
        events::ScopedSubscription entityDeletedSubscription;
        events::ScopedSubscription entityStaticChangedSubscription;
        events::ScopedSubscription transformChangedSubscription;
        events::ScopedSubscription sceneLoadedSubscription;
        events::ScopedSubscription sceneClearedSubscription;
        events::ScopedSubscription prefabInstantiatedSubscription;
        events::ScopedSubscription entityDuplicatedSubscription;

    public:
        explicit LightBVHManager();
        ~LightBVHManager() = default;  // RAII handles cleanup

        // Initialize the manager and set up event subscriptions
        void init();

        // Force rebuild of both BVH trees
        void rebuild();

        // Mark all trees as dirty
        void markDirty();
        void markStaticDirty() { lightBVH.markStaticDirty(); }
        void markDynamicDirty() { lightBVH.markDynamicDirty(); }

        // Update the BVH trees if dirty (call this each frame)
        void update();

        // Query visible lights in the frustum
        void queryFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const
        {
            lightBVH.queryFrustum(frustum, results);
        }

        // State queries
        bool isBuilt() const { return lightBVH.isBuilt(); }
        bool isStaticDirty() const { return lightBVH.isStaticDirty(); }
        bool isDynamicDirty() const { return lightBVH.isDynamicDirty(); }
        bool needsDynamicRebuild() const { return lightBVH.needsDynamicRebuild(); }

        // Mark a specific dynamic light as dirty
        void markDynamicLightDirty(uint32_t entityId) { lightBVH.markDynamicLightDirty(entityId); }

        // Check entity membership
        bool isStaticLight(uint32_t entityId) const { return lightBVH.isStaticLight(entityId); }
        bool isDynamicLight(uint32_t entityId) const { return lightBVH.isDynamicLight(entityId); }

        // Statistics
        size_t getStaticLightCount() const { return lightBVH.getStaticLightCount(); }
        size_t getDynamicLightCount() const { return lightBVH.getDynamicLightCount(); }
        size_t getStaticNodeCount() const { return lightBVH.getStaticNodeCount(); }
        size_t getDynamicNodeCount() const { return lightBVH.getDynamicNodeCount(); }
    };
}
