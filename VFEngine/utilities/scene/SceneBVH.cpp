#include "SceneBVH.hpp"
#include "../threading/ParallelCollect.hpp"
#include "../threading/ParallelView.hpp"

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

        auto& registry = EntityRegistry::getRegistry();
        auto callback = meshBoundsCallback;

        // Materialize dirty set into a vector for indexed parallel access
        std::vector<uint32_t> dirtyIds(dirtyDynamicEntities.begin(), dirtyDynamicEntities.end());
        uint32_t count = static_cast<uint32_t>(dirtyIds.size());

        // Per-thread local results
        uint32_t threadCount = threading::JobSystem::instance().getThreadCount() + 1;
        std::vector<std::vector<std::pair<uint32_t, math::AABB>>> threadResults(threadCount);

        threading::JobSystem::instance().parallelFor(count,
            [&](uint32_t begin, uint32_t end)
            {
                uint32_t slot = begin % threadCount;
                auto& localResults = threadResults[slot];

                for (uint32_t i = begin; i < end; ++i)
                {
                    uint32_t entityId = dirtyIds[i];
                    auto entity = static_cast<entt::entity>(entityId);
                    if (!registry.valid(entity)) continue;
                    if (!registry.all_of<components::MeshComponent, components::WorldTransformComponent>(entity)) continue;

                    const auto& meshComp = registry.get<components::MeshComponent>(entity);
                    if (!meshComp.meshRef.isValid()) continue;

                    const math::AABB* localAABB = callback ? callback(meshComp.meshRef.resolve()) : nullptr;
                    if (!localAABB || !localAABB->isValid()) continue;

                    const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
                    localResults.emplace_back(entityId, localAABB->getTransformed(worldTransform.worldMatrix));
                }
            }, 64);

        // Merge into map
        std::unordered_map<uint32_t, math::AABB> updatedBounds;
        for (auto& v : threadResults)
        {
            for (auto& [id, aabb] : v)
            {
                updatedBounds[id] = aabb;
            }
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
        auto callback = meshBoundsCallback;

        primitives = threading::parallelCollect<math::BVHPrimitive,
            components::TransformComponent,
            components::MeshComponent,
            components::WorldTransformComponent>(
            registry,
            [&registry, &callback](entt::entity entity) -> bool
            {
                const auto& transform = registry.get<components::TransformComponent>(entity);
                if (!transform.isStatic) return false;

                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                if (!meshComp.meshRef.isValid()) return false;

                const math::AABB* localAABB = callback ? callback(meshComp.meshRef.resolve()) : nullptr;
                return localAABB && localAABB->isValid();
            },
            [&registry, &callback](entt::entity entity) -> math::BVHPrimitive
            {
                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
                const math::AABB* localAABB = callback(meshComp.meshRef.resolve());

                math::BVHPrimitive prim;
                prim.bounds = localAABB->getTransformed(worldTransform.worldMatrix);
                prim.entityId = static_cast<uint32_t>(entity);
                return prim;
            });
    }

    void SceneBVH::collectDynamicPrimitives(std::vector<math::BVHPrimitive>& primitives)
    {
        auto& registry = EntityRegistry::getRegistry();
        auto callback = meshBoundsCallback;

        primitives = threading::parallelCollect<math::BVHPrimitive,
            components::TransformComponent,
            components::MeshComponent,
            components::WorldTransformComponent>(
            registry,
            [&registry, &callback](entt::entity entity) -> bool
            {
                const auto& transform = registry.get<components::TransformComponent>(entity);
                if (transform.isStatic) return false;

                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                if (!meshComp.meshRef.isValid()) return false;

                const math::AABB* localAABB = callback ? callback(meshComp.meshRef.resolve()) : nullptr;
                return localAABB && localAABB->isValid();
            },
            [&registry, &callback](entt::entity entity) -> math::BVHPrimitive
            {
                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
                const math::AABB* localAABB = callback(meshComp.meshRef.resolve());

                math::BVHPrimitive prim;
                prim.bounds = localAABB->getTransformed(worldTransform.worldMatrix);
                prim.entityId = static_cast<uint32_t>(entity);
                return prim;
            });
    }
}
