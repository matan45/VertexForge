#include "LightBVH.hpp"

namespace scene
{
    bool LightBVH::hasLightsWithoutWorldTransform(bool checkStatic) const
    {
        auto& registry = EntityRegistry::getRegistry();

        // Check point lights
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

        // Check spot lights
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

        // If there are lights waiting for WorldTransformComponent,
        // keep dirty so we rebuild next frame when transforms are ready
        if (hasLightsWithoutWorldTransform(true))
        {
            // Keep dirty, transforms not ready yet
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

        // If there are lights waiting for WorldTransformComponent,
        // keep dirty so we rebuild next frame when transforms are ready
        if (hasLightsWithoutWorldTransform(false))
        {
            // Keep dirty, transforms not ready yet
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

        std::unordered_map<uint32_t, math::AABB> updatedBounds;
        auto& registry = EntityRegistry::getRegistry();

        for (uint32_t entityId : dirtyDynamicLights)
        {
            auto entity = static_cast<entt::entity>(entityId);
            if (!registry.valid(entity))
            {
                continue;
            }

            math::AABB bounds = computeLightBounds(entity, registry);
            if (bounds.isValid())
            {
                updatedBounds[entityId] = bounds;
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

        {
            auto view = registry.view<components::PointLightComponent,
                                      components::TransformComponent,
                                      components::WorldTransformComponent>();
            for (auto entity : view)
            {
                const auto& transform = view.get<components::TransformComponent>(entity);
                if (!transform.isStatic)
                {
                    continue;
                }

                math::AABB bounds = computeLightBounds(entity, registry);
                if (!bounds.isValid())
                {
                    continue;
                }

                math::BVHPrimitive prim;
                prim.bounds = bounds;
                prim.entityId = static_cast<uint32_t>(entity);
                primitives.push_back(prim);
            }
        }

        {
            auto view = registry.view<components::SpotLightComponent,
                                      components::TransformComponent,
                                      components::WorldTransformComponent>();
            for (auto entity : view)
            {
                const auto& transform = view.get<components::TransformComponent>(entity);
                if (!transform.isStatic)
                {
                    continue;
                }

                math::AABB bounds = computeLightBounds(entity, registry);
                if (!bounds.isValid())
                {
                    continue;
                }

                math::BVHPrimitive prim;
                prim.bounds = bounds;
                prim.entityId = static_cast<uint32_t>(entity);
                primitives.push_back(prim);
            }
        }
    }

    void LightBVH::collectDynamicLightPrimitives(std::vector<math::BVHPrimitive>& primitives)
    {
        auto& registry = EntityRegistry::getRegistry();

        {
            auto view = registry.view<components::PointLightComponent,
                                      components::TransformComponent,
                                      components::WorldTransformComponent>();
            for (auto entity : view)
            {
                const auto& transform = view.get<components::TransformComponent>(entity);
                if (transform.isStatic)
                {
                    continue;
                }

                math::AABB bounds = computeLightBounds(entity, registry);
                if (!bounds.isValid())
                {
                    continue;
                }

                math::BVHPrimitive prim;
                prim.bounds = bounds;
                prim.entityId = static_cast<uint32_t>(entity);
                primitives.push_back(prim);
            }
        }

        {
            auto view = registry.view<components::SpotLightComponent,
                                      components::TransformComponent,
                                      components::WorldTransformComponent>();
            for (auto entity : view)
            {
                const auto& transform = view.get<components::TransformComponent>(entity);
                if (transform.isStatic)
                {
                    continue;
                }

                math::AABB bounds = computeLightBounds(entity, registry);
                if (!bounds.isValid())
                {
                    continue;
                }

                math::BVHPrimitive prim;
                prim.bounds = bounds;
                prim.entityId = static_cast<uint32_t>(entity);
                primitives.push_back(prim);
            }
        }
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
