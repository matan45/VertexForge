#pragma once
#include "../math/BVH.hpp"
#include "../math/LightBounds.hpp"
#include "EntityRegistry.hpp"
#include "../components/Components.hpp"
#include <unordered_set>

namespace scene
{
    // Light type enumeration for BVH primitives
    enum class LightType : uint8_t
    {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    // Dual-tree BVH for static and dynamic lights
    // Follows the same pattern as SceneBVH but specialized for light entities
    class LightBVH
    {
    private:
        math::BVH staticBVH;
        math::BVH dynamicBVH;

        std::unordered_set<uint32_t> staticLightEntities;
        std::unordered_set<uint32_t> dynamicLightEntities;
        std::unordered_set<uint32_t> dirtyDynamicLights;  // Lights needing bounds update

        bool staticDirty = true;
        bool dynamicDirty = true;
        bool staticStructuralChange = true;
        bool dynamicStructuralChange = true;

    public:
        explicit LightBVH() = default;

        // Rebuild the static light BVH (called when static lights change)
        void rebuildStaticLightBVH();

        // Rebuild the dynamic light BVH (full rebuild)
        void rebuildDynamicLightBVH();

        // Refit the dynamic light BVH (update bounds without restructuring)
        void refitDynamicLightBVH();

        // Rebuild both trees
        void rebuildAll()
        {
            rebuildStaticLightBVH();
            rebuildDynamicLightBVH();
        }

        // Mark the static BVH as needing rebuild
        void markStaticDirty()
        {
            staticDirty = true;
            staticStructuralChange = true;
        }

        // Mark the dynamic BVH as needing rebuild
        void markDynamicDirty()
        {
            dynamicDirty = true;
            dynamicStructuralChange = true;
        }

        // Mark a specific dynamic light as needing bounds update
        void markDynamicLightDirty(uint32_t entityId);

        // Mark both trees as dirty
        void markDirty()
        {
            markDynamicDirty();
            markStaticDirty();
        }

        // Check dirty state
        bool isStaticDirty() const { return staticDirty; }
        bool isDynamicDirty() const { return dynamicDirty; }
        bool needsDynamicRebuild() const { return dynamicStructuralChange; }
        size_t getDirtyDynamicLightCount() const { return dirtyDynamicLights.size(); }
        bool isDirty() const { return staticDirty || dynamicDirty; }

        // Conditional rebuilds
        void rebuildStaticIfDirty()
        {
            if (staticDirty)
            {
                rebuildStaticLightBVH();
            }
        }

        // Smart update: rebuilds or refits based on structural change flag
        void updateDynamicLightBVH();

        void rebuildDynamicIfDirty()
        {
            if (dynamicDirty)
            {
                updateDynamicLightBVH();
            }
        }

        // Query both trees and merge results
        void queryFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const;

        // Query specific trees (for debugging/optimization)
        void queryStaticFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const
        {
            staticBVH.queryFrustum(frustum, results);
        }

        void queryDynamicFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const
        {
            dynamicBVH.queryFrustum(frustum, results);
        }

        // Check entity membership
        bool isStaticLight(uint32_t entityId) const
        {
            return staticLightEntities.find(entityId) != staticLightEntities.end();
        }

        bool isDynamicLight(uint32_t entityId) const
        {
            return dynamicLightEntities.find(entityId) != dynamicLightEntities.end();
        }

        // Statistics
        size_t getStaticNodeCount() const { return staticBVH.getNodeCount(); }
        size_t getDynamicNodeCount() const { return dynamicBVH.getNodeCount(); }
        size_t getStaticLightCount() const { return staticBVH.getPrimitiveCount(); }
        size_t getDynamicLightCount() const { return dynamicBVH.getPrimitiveCount(); }

        bool isBuilt() const
        {
            return staticBVH.isBuilt() || dynamicBVH.isBuilt();
        }

        size_t getNodeCount() const
        {
            return staticBVH.getNodeCount() + dynamicBVH.getNodeCount();
        }

        size_t getLightCount() const
        {
            return staticBVH.getPrimitiveCount() + dynamicBVH.getPrimitiveCount();
        }

        void clear();

    private:
        // Collect primitives for static lights (isStatic == true)
        void collectStaticLightPrimitives(std::vector<math::BVHPrimitive>& primitives);

        // Collect primitives for dynamic lights (isStatic == false)
        void collectDynamicLightPrimitives(std::vector<math::BVHPrimitive>& primitives);

        // Compute world-space AABB for a light entity
        math::AABB computeLightBounds(entt::entity entity, entt::registry& registry);
    };
}
