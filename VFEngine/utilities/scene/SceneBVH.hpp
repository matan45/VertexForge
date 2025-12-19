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

    // Two-level BVH manager for spatial queries on entities
    // Separates static and dynamic entities into separate trees for efficient updates
    class SceneBVH
    {
    public:
        SceneBVH() = default;

        // Set callback to retrieve mesh bounding boxes
        void setMeshBoundsCallback(MeshBoundsCallback callback)
        {
            meshBoundsCallback_ = std::move(callback);
        }

        // === Rebuild Methods ===

        // Rebuild static BVH only (entities with StaticEntityComponent)
        void rebuildStaticBVH()
        {
            std::vector<math::BVHPrimitive> primitives;
            collectStaticPrimitives(primitives);

            staticEntities_.clear();
            for (const auto& prim : primitives)
            {
                staticEntities_.insert(prim.entityId);
            }

            staticBVH_.build(std::move(primitives));
            staticDirty_ = false;
        }

        // Rebuild dynamic BVH only (entities without StaticEntityComponent)
        void rebuildDynamicBVH()
        {
            std::vector<math::BVHPrimitive> primitives;
            collectDynamicPrimitives(primitives);

            dynamicEntities_.clear();
            for (const auto& prim : primitives)
            {
                dynamicEntities_.insert(prim.entityId);
            }

            dynamicBVH_.build(std::move(primitives));
            dynamicDirty_ = false;
        }

        // Rebuild both trees (scene load, major changes)
        void rebuildAll()
        {
            rebuildStaticBVH();
            rebuildDynamicBVH();
        }

        // Legacy method - rebuilds both trees
        void rebuild()
        {
            rebuildAll();
        }

        // === Dirty State Management ===

        void markStaticDirty()
        {
            staticDirty_ = true;
        }

        void markDynamicDirty()
        {
            dynamicDirty_ = true;
        }

        // Legacy method - marks both as dirty
        void markDirty()
        {
            staticDirty_ = true;
            dynamicDirty_ = true;
        }

        bool isStaticDirty() const
        {
            return staticDirty_;
        }

        bool isDynamicDirty() const
        {
            return dynamicDirty_;
        }

        // Legacy method - returns true if either is dirty
        bool isDirty() const
        {
            return staticDirty_ || dynamicDirty_;
        }

        void rebuildStaticIfDirty()
        {
            if (staticDirty_)
            {
                rebuildStaticBVH();
            }
        }

        void rebuildDynamicIfDirty()
        {
            if (dynamicDirty_)
            {
                rebuildDynamicBVH();
            }
        }

        // Legacy method
        void rebuildIfDirty()
        {
            rebuildStaticIfDirty();
            rebuildDynamicIfDirty();
        }

        // === Query API ===

        // Query both trees and merge results
        void queryFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const
        {
            results.clear();

            // Reserve estimated space
            results.reserve(staticEntities_.size() + dynamicEntities_.size());

            // Query static tree
            if (staticBVH_.isBuilt())
            {
                staticBVH_.queryFrustum(frustum, results);
            }

            // Query dynamic tree and append
            if (dynamicBVH_.isBuilt())
            {
                std::vector<uint32_t> dynamicResults;
                dynamicBVH_.queryFrustum(frustum, dynamicResults);
                results.insert(results.end(), dynamicResults.begin(), dynamicResults.end());
            }
        }

        // Query specific trees (for debugging/optimization)
        void queryStaticFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const
        {
            staticBVH_.queryFrustum(frustum, results);
        }

        void queryDynamicFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const
        {
            dynamicBVH_.queryFrustum(frustum, results);
        }

        // === Entity Tracking ===

        // Check if an entity is in the static tree
        bool isStaticEntity(uint32_t entityId) const
        {
            return staticEntities_.find(entityId) != staticEntities_.end();
        }

        // Check if an entity is in the dynamic tree
        bool isDynamicEntity(uint32_t entityId) const
        {
            return dynamicEntities_.find(entityId) != dynamicEntities_.end();
        }

        // === Statistics ===

        size_t getStaticNodeCount() const { return staticBVH_.getNodeCount(); }
        size_t getDynamicNodeCount() const { return dynamicBVH_.getNodeCount(); }
        size_t getStaticEntityCount() const { return staticBVH_.getPrimitiveCount(); }
        size_t getDynamicEntityCount() const { return dynamicBVH_.getPrimitiveCount(); }

        // Legacy methods
        bool isBuilt() const
        {
            return staticBVH_.isBuilt() || dynamicBVH_.isBuilt();
        }

        size_t getNodeCount() const
        {
            return staticBVH_.getNodeCount() + dynamicBVH_.getNodeCount();
        }

        size_t getEntityCount() const
        {
            return staticBVH_.getPrimitiveCount() + dynamicBVH_.getPrimitiveCount();
        }

        // Clear both BVHs
        void clear()
        {
            staticBVH_.clear();
            dynamicBVH_.clear();
            staticEntities_.clear();
            dynamicEntities_.clear();
            staticDirty_ = true;
            dynamicDirty_ = true;
        }

    private:
        math::BVH staticBVH_;                          // Infrequently rebuilt
        math::BVH dynamicBVH_;                         // Frequently rebuilt

        MeshBoundsCallback meshBoundsCallback_;

        std::unordered_set<uint32_t> staticEntities_;  // Track static entity IDs
        std::unordered_set<uint32_t> dynamicEntities_; // Track dynamic entity IDs

        bool staticDirty_ = true;
        bool dynamicDirty_ = true;

        // Collect primitives for static entities (TransformComponent.isStatic == true)
        void collectStaticPrimitives(std::vector<math::BVHPrimitive>& primitives)
        {
            auto& registry = EntityRegistry::getRegistry();
            auto view = registry.view<components::TransformComponent,
                                      components::MeshComponent,
                                      components::WorldTransformComponent>();

            for (auto entity : view)
            {
                const auto& transform = view.get<components::TransformComponent>(entity);

                // Only collect static entities
                if (!transform.isStatic)
                {
                    continue;
                }

                const auto& meshComp = view.get<components::MeshComponent>(entity);
                if (meshComp.meshPath.empty())
                {
                    continue;
                }

                const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

                const math::AABB* localAABB = nullptr;
                if (meshBoundsCallback_)
                {
                    localAABB = meshBoundsCallback_(meshComp.meshPath);
                }

                if (!localAABB || !localAABB->isValid())
                {
                    continue;
                }

                math::AABB worldAABB = localAABB->getTransformed(worldTransform.worldMatrix);

                math::BVHPrimitive prim;
                prim.bounds = worldAABB;
                prim.entityId = static_cast<uint32_t>(entity);

                primitives.push_back(prim);
            }
        }

        // Collect primitives for dynamic entities (TransformComponent.isStatic == false)
        void collectDynamicPrimitives(std::vector<math::BVHPrimitive>& primitives)
        {
            auto& registry = EntityRegistry::getRegistry();
            auto view = registry.view<components::TransformComponent,
                                      components::MeshComponent,
                                      components::WorldTransformComponent>();

            for (auto entity : view)
            {
                const auto& transform = view.get<components::TransformComponent>(entity);

                // Only collect dynamic entities
                if (transform.isStatic)
                {
                    continue;
                }

                const auto& meshComp = view.get<components::MeshComponent>(entity);
                if (meshComp.meshPath.empty())
                {
                    continue;
                }

                const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

                const math::AABB* localAABB = nullptr;
                if (meshBoundsCallback_)
                {
                    localAABB = meshBoundsCallback_(meshComp.meshPath);
                }

                if (!localAABB || !localAABB->isValid())
                {
                    continue;
                }

                math::AABB worldAABB = localAABB->getTransformed(worldTransform.worldMatrix);

                math::BVHPrimitive prim;
                prim.bounds = worldAABB;
                prim.entityId = static_cast<uint32_t>(entity);

                primitives.push_back(prim);
            }
        }
    };
}
