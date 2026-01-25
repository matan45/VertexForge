#pragma once
#include "../math/BVH.hpp"
#include "../math/LightBounds.hpp"
#include "EntityRegistry.hpp"
#include "../components/Components.hpp"
#include <algorithm>
#include <unordered_set>

namespace scene
{
    class LightBVH
    {
    private:
        math::BVH staticBVH;
        math::BVH dynamicBVH;

        std::unordered_set<uint32_t> staticLightEntities;
        std::unordered_set<uint32_t> dynamicLightEntities;
        std::unordered_set<uint32_t> dirtyDynamicLights;

        std::vector<uint32_t> staticDirectionalLights;
        std::vector<uint32_t> dynamicDirectionalLights;

        bool staticDirty = true;
        bool dynamicDirty = true;
        bool staticStructuralChange = true;
        bool dynamicStructuralChange = true;

        void rebuildStaticLightBVH();
        void rebuildDynamicLightBVH();
        void refitDynamicLightBVH();
        void updateDynamicLightBVH();

        void collectStaticLightPrimitives(std::vector<math::BVHPrimitive>& primitives);
        void collectDynamicLightPrimitives(std::vector<math::BVHPrimitive>& primitives);
        void collectStaticDirectionalLights();
        void collectDynamicDirectionalLights();
        math::AABB computeLightBounds(entt::entity entity, entt::registry& registry);

    public:
        explicit LightBVH() = default;

        void markStaticDirty()
        {
            staticDirty = true;
            staticStructuralChange = true;
        }

        void markDynamicDirty()
        {
            dynamicDirty = true;
            dynamicStructuralChange = true;
        }

        void markDynamicLightDirty(uint32_t entityId);

        void markDirty()
        {
            markDynamicDirty();
            markStaticDirty();
        }

        void rebuildStaticIfDirty()
        {
            if (staticDirty)
            {
                rebuildStaticLightBVH();
            }
        }

        void rebuildDynamicIfDirty()
        {
            if (dynamicDirty)
            {
                updateDynamicLightBVH();
            }
        }

        bool isStaticLight(uint32_t entityId) const
        {
            if (staticLightEntities.find(entityId) != staticLightEntities.end())
            {
                return true;
            }
            return std::find(staticDirectionalLights.begin(), staticDirectionalLights.end(), entityId)
                   != staticDirectionalLights.end();
        }

        bool isDynamicLight(uint32_t entityId) const
        {
            if (dynamicLightEntities.find(entityId) != dynamicLightEntities.end())
            {
                return true;
            }
            return std::find(dynamicDirectionalLights.begin(), dynamicDirectionalLights.end(), entityId)
                   != dynamicDirectionalLights.end();
        }

        size_t getStaticNodeCount() const { return staticBVH.getNodeCount(); }
        size_t getDynamicNodeCount() const { return dynamicBVH.getNodeCount(); }
        size_t getStaticLightCount() const { return staticBVH.getPrimitiveCount() + staticDirectionalLights.size(); }
        size_t getDynamicLightCount() const { return dynamicBVH.getPrimitiveCount() + dynamicDirectionalLights.size(); }

        // Returns entity IDs of point/spot lights intersecting the frustum
        // Directional lights are always included (global/infinite range)
        void queryFrustum(const math::Frustum& frustum, std::vector<uint32_t>& visibleLights) const
        {
            visibleLights.clear();

            staticBVH.queryFrustumAppend(frustum, visibleLights);
            dynamicBVH.queryFrustumAppend(frustum, visibleLights);

            for (uint32_t entityId : staticDirectionalLights)
            {
                visibleLights.push_back(entityId);
            }
            for (uint32_t entityId : dynamicDirectionalLights)
            {
                visibleLights.push_back(entityId);
            }
        }
    };
}
