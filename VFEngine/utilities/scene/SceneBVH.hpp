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

    // Scene-level BVH manager for spatial queries on entities
    class SceneBVH
    {
    public:
        SceneBVH() = default;

        // Set callback to retrieve mesh bounding boxes
        void setMeshBoundsCallback(MeshBoundsCallback callback)
        {
            meshBoundsCallback_ = std::move(callback);
        }

        // Rebuild BVH from all mesh entities in scene
        // Call this when scene changes significantly (load, many adds/removes)
        void rebuild()
        {
            std::vector<math::BVHPrimitive> primitives;

            auto& registry = EntityRegistry::getRegistry();
            auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

            for (auto entity : view)
            {
                const auto& meshComp = view.get<components::MeshComponent>(entity);
                const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

                if (meshComp.meshPath.empty())
                {
                    continue;
                }

                // Get local bounding box from mesh
                const math::AABB* localAABB = nullptr;
                if (meshBoundsCallback_)
                {
                    localAABB = meshBoundsCallback_(meshComp.meshPath);
                }

                if (!localAABB || !localAABB->isValid())
                {
                    continue;
                }

                // Transform to world space
                math::AABB worldAABB = localAABB->getTransformed(worldTransform.worldMatrix);

                math::BVHPrimitive prim;
                prim.bounds = worldAABB;
                prim.entityId = static_cast<uint32_t>(entity);

                primitives.push_back(prim);
            }

            bvh_.build(std::move(primitives));
            entitySet_.clear();

            // Track which entities are in BVH
            for (auto entity : view)
            {
                entitySet_.insert(static_cast<uint32_t>(entity));
            }

            dirty_ = false;
        }

        // Mark BVH as needing rebuild
        void markDirty()
        {
            dirty_ = true;
        }

        // Check if BVH needs rebuild
        bool isDirty() const
        {
            return dirty_;
        }

        // Rebuild if dirty
        void rebuildIfDirty()
        {
            if (dirty_)
            {
                rebuild();
            }
        }

        // Query entities visible in frustum
        // Returns entity IDs (entt::entity cast to uint32_t)
        void queryFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const
        {
            bvh_.queryFrustum(frustum, results);
        }

        // Check if BVH has been built
        bool isBuilt() const
        {
            return bvh_.isBuilt();
        }

        // Get statistics
        size_t getNodeCount() const { return bvh_.getNodeCount(); }
        size_t getEntityCount() const { return bvh_.getPrimitiveCount(); }

        // Clear BVH
        void clear()
        {
            bvh_.clear();
            entitySet_.clear();
            dirty_ = true;
        }

    private:
        math::BVH bvh_;
        MeshBoundsCallback meshBoundsCallback_;
        std::unordered_set<uint32_t> entitySet_;
        bool dirty_ = true;
    };
}
