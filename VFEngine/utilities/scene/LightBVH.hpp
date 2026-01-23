#pragma once
#include "../math/BVH.hpp"
#include "../math/LightBounds.hpp"
#include "EntityRegistry.hpp"
#include "../components/Components.hpp"
#include <algorithm>
#include <unordered_set>

namespace scene
{
    // Dual-tree BVH for static and dynamic lights
    // Follows the same pattern as SceneBVH but specialized for light entities
    // NOTE: Directional lights are stored separately (always visible, no BVH needed)
    class LightBVH
    {
    private:
        math::BVH staticBVH;
        math::BVH dynamicBVH;

        std::unordered_set<uint32_t> staticLightEntities;      // Point/spot lights in static BVH
        std::unordered_set<uint32_t> dynamicLightEntities;     // Point/spot lights in dynamic BVH
        std::unordered_set<uint32_t> dirtyDynamicLights;       // Lights needing bounds update

        // Directional lights are stored separately - they have infinite bounds
        // and are always included in query results (no spatial culling benefit)
        std::vector<uint32_t> staticDirectionalLights;
        std::vector<uint32_t> dynamicDirectionalLights;

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

        // Mark the static BVH as needing full rebuild (structural change)
        // Use this when: lights added/removed from static tree, entity deleted, static flag changed
        void markStaticDirty()
        {
            staticDirty = true;
            staticStructuralChange = true;
        }

        // Mark the dynamic BVH as needing full rebuild (structural change)
        // Use this when: lights added/removed from dynamic tree, entity deleted, static flag changed
        void markDynamicDirty()
        {
            dynamicDirty = true;
            dynamicStructuralChange = true;
        }

        // Mark a specific dynamic light as needing bounds update only (refit, no structural change)
        // Use this when: light position/direction/range changed but entity stays in same tree
        // This allows efficient incremental updates without full tree rebuild
        void markDynamicLightDirty(uint32_t entityId);

        // Mark both trees as needing full rebuild (structural change to both)
        // Use this when: entity moves between static/dynamic trees, scene load/clear
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

        // Check entity membership (includes both local lights in BVH and directional lights)
        bool isStaticLight(uint32_t entityId) const
        {
            if (staticLightEntities.find(entityId) != staticLightEntities.end())
            {
                return true;
            }
            // Also check directional lights
            return std::find(staticDirectionalLights.begin(), staticDirectionalLights.end(), entityId)
                   != staticDirectionalLights.end();
        }

        bool isDynamicLight(uint32_t entityId) const
        {
            if (dynamicLightEntities.find(entityId) != dynamicLightEntities.end())
            {
                return true;
            }
            // Also check directional lights
            return std::find(dynamicDirectionalLights.begin(), dynamicDirectionalLights.end(), entityId)
                   != dynamicDirectionalLights.end();
        }

        // Statistics
        size_t getStaticNodeCount() const { return staticBVH.getNodeCount(); }
        size_t getDynamicNodeCount() const { return dynamicBVH.getNodeCount(); }
        size_t getStaticLightCount() const { return staticBVH.getPrimitiveCount() + staticDirectionalLights.size(); }
        size_t getDynamicLightCount() const { return dynamicBVH.getPrimitiveCount() + dynamicDirectionalLights.size(); }

        // Directional light counts (always visible, not in BVH)
        size_t getStaticDirectionalLightCount() const { return staticDirectionalLights.size(); }
        size_t getDynamicDirectionalLightCount() const { return dynamicDirectionalLights.size(); }
        size_t getDirectionalLightCount() const { return staticDirectionalLights.size() + dynamicDirectionalLights.size(); }

        // Local light counts (point/spot - in BVH)
        size_t getStaticLocalLightCount() const { return staticBVH.getPrimitiveCount(); }
        size_t getDynamicLocalLightCount() const { return dynamicBVH.getPrimitiveCount(); }
        size_t getLocalLightCount() const { return staticBVH.getPrimitiveCount() + dynamicBVH.getPrimitiveCount(); }

        bool isBuilt() const
        {
            return staticBVH.isBuilt() || dynamicBVH.isBuilt() ||
                   !staticDirectionalLights.empty() || !dynamicDirectionalLights.empty();
        }

        size_t getNodeCount() const
        {
            return staticBVH.getNodeCount() + dynamicBVH.getNodeCount();
        }

        size_t getLightCount() const
        {
            return staticBVH.getPrimitiveCount() + dynamicBVH.getPrimitiveCount() +
                   staticDirectionalLights.size() + dynamicDirectionalLights.size();
        }

        void clear();

    private:
        // Collect primitives for static local lights (point/spot with isStatic == true)
        void collectStaticLightPrimitives(std::vector<math::BVHPrimitive>& primitives);

        // Collect primitives for dynamic local lights (point/spot with isStatic == false)
        void collectDynamicLightPrimitives(std::vector<math::BVHPrimitive>& primitives);

        // Collect static directional lights (isStatic == true)
        void collectStaticDirectionalLights();

        // Collect dynamic directional lights (isStatic == false)
        void collectDynamicDirectionalLights();

        // Compute world-space AABB for a local light entity (point/spot only)
        math::AABB computeLightBounds(entt::entity entity, entt::registry& registry);
    };
}
