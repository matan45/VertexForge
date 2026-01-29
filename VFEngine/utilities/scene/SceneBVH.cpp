#include "SceneBVH.hpp"

namespace scene
{
    bool SceneBVH::hasMeshesWithoutWorldTransform(bool checkStatic) const
    {
        auto& registry = EntityRegistry::getRegistry();

        auto view = registry.view<components::MeshComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
            if (transform.isStatic == checkStatic)
            {
                if (!registry.all_of<components::WorldTransformComponent>(entity))
                {
                    return true;
                }
            }
        }

        return false;
    }

    void SceneBVH::rebuildStaticBVH()
    {
        std::vector<math::BVHPrimitive> primitives;
        collectStaticPrimitives(primitives);

        staticEntities.clear();
        for (const auto& prim : primitives)
        {
            staticEntities.insert(prim.entityId);
        }

        staticBVH.build(std::move(primitives));

        if (hasMeshesWithoutWorldTransform(true))
        {
            return;
        }

        staticDirty = false;
        staticStructuralChange = false;
    }

    void SceneBVH::rebuildDynamicBVH()
    {
        std::vector<math::BVHPrimitive> primitives;
        collectDynamicPrimitives(primitives);

        dynamicEntities.clear();
        for (const auto& prim : primitives)
        {
            dynamicEntities.insert(prim.entityId);
        }

        dynamicBVH.build(std::move(primitives));

        if (hasMeshesWithoutWorldTransform(false))
        {
            return;
        }

        dirtyDynamicEntities.clear();
        dynamicDirty = false;
        dynamicStructuralChange = false;
    }

    void SceneBVH::refitDynamicBVH()
    {
        if (dirtyDynamicEntities.empty())
        {
            dynamicDirty = false;
            return;
        }

        std::unordered_map<uint32_t, math::AABB> updatedBounds;
        auto& registry = EntityRegistry::getRegistry();

        for (uint32_t entityId : dirtyDynamicEntities)
        {
            auto entity = static_cast<entt::entity>(entityId);
            if (!registry.valid(entity))
            {
                continue;
            }

            if (!registry.all_of<components::MeshComponent, components::WorldTransformComponent>(entity))
            {
                continue;
            }

            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            if (meshComp.meshPath.empty())
            {
                continue;
            }

            const math::AABB* localAABB = nullptr;
            if (meshBoundsCallback)
            {
                localAABB = meshBoundsCallback(meshComp.meshPath);
            }

            if (!localAABB || !localAABB->isValid())
            {
                continue;
            }

            const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
            updatedBounds[entityId] = localAABB->getTransformed(worldTransform.worldMatrix);
        }

        dynamicBVH.updateEntitiesBounds(updatedBounds);

        dirtyDynamicEntities.clear();
        dynamicDirty = false;
    }

    void SceneBVH::markDynamicEntityDirty(uint32_t entityId)
    {
        if (dynamicEntities.find(entityId) != dynamicEntities.end())
        {
            dirtyDynamicEntities.insert(entityId);
            dynamicDirty = true;
        }
    }

    void SceneBVH::updateDynamicBVH()
    {
        if (!dynamicDirty)
        {
            return;
        }

        if (dynamicStructuralChange)
        {
            rebuildDynamicBVH();
        }
        else
        {
            refitDynamicBVH();
        }
    }

    void SceneBVH::queryFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const
    {
        results.clear();
        results.reserve(staticEntities.size() + dynamicEntities.size());

        if (staticBVH.isBuilt())
        {
            staticBVH.queryFrustumAppend(frustum, results);
        }

        if (dynamicBVH.isBuilt())
        {
            dynamicBVH.queryFrustumAppend(frustum, results);
        }
    }

    void SceneBVH::clear()
    {
        staticBVH.clear();
        dynamicBVH.clear();
        staticEntities.clear();
        dynamicEntities.clear();
        dirtyDynamicEntities.clear();
        staticDirty = true;
        dynamicDirty = true;
        staticStructuralChange = true;
        dynamicStructuralChange = true;
    }

    void SceneBVH::collectStaticPrimitives(std::vector<math::BVHPrimitive>& primitives)
    {
        auto& registry = EntityRegistry::getRegistry();
        auto view = registry.view<components::TransformComponent,
                                  components::MeshComponent,
                                  components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
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
            if (meshBoundsCallback)
            {
                localAABB = meshBoundsCallback(meshComp.meshPath);
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

    void SceneBVH::collectDynamicPrimitives(std::vector<math::BVHPrimitive>& primitives)
    {
        auto& registry = EntityRegistry::getRegistry();
        auto view = registry.view<components::TransformComponent,
                                  components::MeshComponent,
                                  components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
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
            if (meshBoundsCallback)
            {
                localAABB = meshBoundsCallback(meshComp.meshPath);
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
}
