#include "LightBVH.hpp"
#include "../threading/ParallelCollect.hpp"
#include "../threading/JobSystem.hpp"

namespace scene
{
    bool LightBVH::hasLightsWithoutWorldTransform(bool checkStatic) const
    {
        auto& registry = EntityRegistry::getRegistry();

        auto pointView = registry.view<components::PointLightComponent, components::TransformComponent>();
        for (auto entity : pointView)
        {
            const auto& transform = pointView.get<components::TransformComponent>(entity);
            if (transform.isStatic == checkStatic)
            {
                if (!registry.all_of<components::WorldTransformComponent>(entity))
                {
                    return true;
                }
            }
        }

        auto spotView = registry.view<components::SpotLightComponent, components::TransformComponent>();
        for (auto entity : spotView)
        {
            const auto& transform = spotView.get<components::TransformComponent>(entity);
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

    void LightBVH::rebuildStaticLightBVH()
    {
        std::vector<math::BVHPrimitive> primitives;
        collectStaticLightPrimitives(primitives);

        staticLightEntities.clear();
        for (const auto& prim : primitives)
        {
            staticLightEntities.insert(prim.entityId);
        }

        staticBVH.build(std::move(primitives));
        collectStaticDirectionalLights();

        if (hasLightsWithoutWorldTransform(true))
        {
            return;
        }

        staticDirty = false;
        staticStructuralChange = false;
    }

    void LightBVH::rebuildDynamicLightBVH()
    {
        std::vector<math::BVHPrimitive> primitives;
        collectDynamicLightPrimitives(primitives);

        dynamicLightEntities.clear();
        for (const auto& prim : primitives)
        {
            dynamicLightEntities.insert(prim.entityId);
        }

        dynamicBVH.build(std::move(primitives));
        collectDynamicDirectionalLights();

        if (hasLightsWithoutWorldTransform(false))
        {
            return;
        }

        dirtyDynamicLights.clear();
        dynamicDirty = false;
        dynamicStructuralChange = false;
    }

    void LightBVH::refitDynamicLightBVH()
    {
        if (dirtyDynamicLights.empty())
        {
            dynamicDirty = false;
            return;
        }

        auto& registry = EntityRegistry::getRegistry();

        // Materialize dirty set for indexed parallel access
        std::vector<uint32_t> dirtyIds(dirtyDynamicLights.begin(), dirtyDynamicLights.end());
        uint32_t count = static_cast<uint32_t>(dirtyIds.size());

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

                    math::AABB bounds = computeLightBounds(entity, registry);
                    if (bounds.isValid())
                    {
                        localResults.emplace_back(entityId, bounds);
                    }
                }
            }, 64);

        std::unordered_map<uint32_t, math::AABB> updatedBounds;
        for (auto& v : threadResults)
        {
            for (auto& [id, aabb] : v)
            {
                updatedBounds[id] = aabb;
            }
        }

        dynamicBVH.updateEntitiesBounds(updatedBounds);

        dirtyDynamicLights.clear();
        dynamicDirty = false;
    }

    void LightBVH::markDynamicLightDirty(uint32_t entityId)
    {
        if (dynamicLightEntities.find(entityId) != dynamicLightEntities.end())
        {
            dirtyDynamicLights.insert(entityId);
            dynamicDirty = true;
        }
    }

    void LightBVH::updateDynamicLightBVH()
    {
        if (!dynamicDirty)
        {
            return;
        }

        if (dynamicStructuralChange)
        {
            rebuildDynamicLightBVH();
        }
        else
        {
            refitDynamicLightBVH();
        }
    }

    math::AABB LightBVH::computeLightBounds(entt::entity entity, entt::registry& registry)
    {
        if (!registry.all_of<components::WorldTransformComponent>(entity))
        {
            return math::AABB();
        }

        const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
        glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);
        glm::vec3 direction = -glm::normalize(glm::vec3(worldTransform.worldMatrix[2]));

        if (registry.all_of<components::PointLightComponent>(entity))
        {
            const auto& light = registry.get<components::PointLightComponent>(entity);
            return math::LightBounds::computePointLightAABB(position, light.radius);
        }

        if (registry.all_of<components::SpotLightComponent>(entity))
        {
            const auto& light = registry.get<components::SpotLightComponent>(entity);
            return math::LightBounds::computeSpotLightAABB(position, direction, light.range, light.outerAngle);
        }

        return math::AABB();
    }

    void LightBVH::collectStaticLightPrimitives(std::vector<math::BVHPrimitive>& primitives)
    {
        auto& registry = EntityRegistry::getRegistry();

        auto filterStatic = [this, &registry](entt::entity entity) -> bool
        {
            const auto& transform = registry.get<components::TransformComponent>(entity);
            if (!transform.isStatic) return false;
            math::AABB bounds = computeLightBounds(entity, registry);
            return bounds.isValid();
        };

        auto toBVHPrim = [this, &registry](entt::entity entity) -> math::BVHPrimitive
        {
            math::BVHPrimitive prim;
            prim.bounds = computeLightBounds(entity, registry);
            prim.entityId = static_cast<uint32_t>(entity);
            return prim;
        };

        auto pointPrims = threading::parallelCollect<math::BVHPrimitive,
            components::PointLightComponent,
            components::TransformComponent,
            components::WorldTransformComponent>(registry, filterStatic, toBVHPrim);

        auto spotPrims = threading::parallelCollect<math::BVHPrimitive,
            components::SpotLightComponent,
            components::TransformComponent,
            components::WorldTransformComponent>(registry, filterStatic, toBVHPrim);

        primitives.reserve(pointPrims.size() + spotPrims.size());
        primitives.insert(primitives.end(),
            std::make_move_iterator(pointPrims.begin()),
            std::make_move_iterator(pointPrims.end()));
        primitives.insert(primitives.end(),
            std::make_move_iterator(spotPrims.begin()),
            std::make_move_iterator(spotPrims.end()));
    }

    void LightBVH::collectDynamicLightPrimitives(std::vector<math::BVHPrimitive>& primitives)
    {
        auto& registry = EntityRegistry::getRegistry();

        auto filterDynamic = [this, &registry](entt::entity entity) -> bool
        {
            const auto& transform = registry.get<components::TransformComponent>(entity);
            if (transform.isStatic) return false;
            math::AABB bounds = computeLightBounds(entity, registry);
            return bounds.isValid();
        };

        auto toBVHPrim = [this, &registry](entt::entity entity) -> math::BVHPrimitive
        {
            math::BVHPrimitive prim;
            prim.bounds = computeLightBounds(entity, registry);
            prim.entityId = static_cast<uint32_t>(entity);
            return prim;
        };

        auto pointPrims = threading::parallelCollect<math::BVHPrimitive,
            components::PointLightComponent,
            components::TransformComponent,
            components::WorldTransformComponent>(registry, filterDynamic, toBVHPrim);

        auto spotPrims = threading::parallelCollect<math::BVHPrimitive,
            components::SpotLightComponent,
            components::TransformComponent,
            components::WorldTransformComponent>(registry, filterDynamic, toBVHPrim);

        primitives.reserve(pointPrims.size() + spotPrims.size());
        primitives.insert(primitives.end(),
            std::make_move_iterator(pointPrims.begin()),
            std::make_move_iterator(pointPrims.end()));
        primitives.insert(primitives.end(),
            std::make_move_iterator(spotPrims.begin()),
            std::make_move_iterator(spotPrims.end()));
    }

    void LightBVH::collectStaticDirectionalLights()
    {
        staticDirectionalLights.clear();
        auto& registry = EntityRegistry::getRegistry();

        auto view = registry.view<components::DirectionalLightComponent,
                                  components::TransformComponent>();
        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
            if (!transform.isStatic)
            {
                continue;
            }

            staticDirectionalLights.push_back(static_cast<uint32_t>(entity));
        }
    }

    void LightBVH::collectDynamicDirectionalLights()
    {
        dynamicDirectionalLights.clear();
        auto& registry = EntityRegistry::getRegistry();

        auto view = registry.view<components::DirectionalLightComponent,
                                  components::TransformComponent>();
        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
            if (transform.isStatic)
            {
                continue;
            }

            dynamicDirectionalLights.push_back(static_cast<uint32_t>(entity));
        }
    }
}
