#pragma once
#include "../math/BVH.hpp"
#include "EntityRegistry.hpp"
#include "../components/Components.hpp"
#include <functional>
#include <unordered_set>

namespace scene
{
    // Callback to get mesh bounding box by path
    using MeshBoundsCallback = std::function<const math::AABB*(const std::string&)>;

    class SceneBVH
    {
    private:
        math::BVH staticBVH; 
        math::BVH dynamicBVH;

        MeshBoundsCallback meshBoundsCallback;

        std::unordered_set<uint32_t> staticEntities; 
        std::unordered_set<uint32_t> dynamicEntities; 
        std::unordered_set<uint32_t> dirtyDynamicEntities; // Entities needing bounds update

        bool staticDirty = true;
        bool dynamicDirty = true;
        bool staticStructuralChange = true; 
        bool dynamicStructuralChange = true;

    public:
        explicit SceneBVH() = default;

        void setMeshBoundsCallback(MeshBoundsCallback callback)
        {
            meshBoundsCallback = std::move(callback);
        }

        void rebuildStaticBVH();
        void rebuildDynamicBVH();

        void refitDynamicBVH();

        void rebuildAll()
        {
            rebuildStaticBVH();
            rebuildDynamicBVH();
        }


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

        void markDynamicEntityDirty(uint32_t entityId);

        void markDirty()
        {
            markDynamicDirty();
            markStaticDirty();
        }

        bool isStaticDirty() const
        {
            return staticDirty;
        }

        bool isDynamicDirty() const
        {
            return dynamicDirty;
        }

        bool needsDynamicRebuild() const
        {
            return dynamicStructuralChange;
        }

        size_t getDirtyDynamicEntityCount() const
        {
            return dirtyDynamicEntities.size();
        }

        bool isDirty() const
        {
            return staticDirty || dynamicDirty;
        }

        void rebuildStaticIfDirty()
        {
            if (staticDirty)
            {
                rebuildStaticBVH();
            }
        }

        void updateDynamicBVH();

        void rebuildDynamicIfDirty()
        {
            if (dynamicDirty)
            {
                updateDynamicBVH();
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
        
        bool isStaticEntity(uint32_t entityId) const
        {
            return staticEntities.find(entityId) != staticEntities.end();
        }
        
        bool isDynamicEntity(uint32_t entityId) const
        {
            return dynamicEntities.find(entityId) != dynamicEntities.end();
        }

        // === Statistics ===
        size_t getStaticNodeCount() const { return staticBVH.getNodeCount(); }
        size_t getDynamicNodeCount() const { return dynamicBVH.getNodeCount(); }
        size_t getStaticEntityCount() const { return staticBVH.getPrimitiveCount(); }
        size_t getDynamicEntityCount() const { return dynamicBVH.getPrimitiveCount(); }

        // Legacy methods
        bool isBuilt() const
        {
            return staticBVH.isBuilt() || dynamicBVH.isBuilt();
        }

        size_t getNodeCount() const
        {
            return staticBVH.getNodeCount() + dynamicBVH.getNodeCount();
        }

        size_t getEntityCount() const
        {
            return staticBVH.getPrimitiveCount() + dynamicBVH.getPrimitiveCount();
        }

        void clear();

    private:
        void collectStaticPrimitives(std::vector<math::BVHPrimitive>& primitives);

        void collectDynamicPrimitives(std::vector<math::BVHPrimitive>& primitives);
    };
}
