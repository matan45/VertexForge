#include "LightBVH.hpp"

namespace scene
{
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

        // Collect new bounds for dirty lights
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

        // Apply batch update to BVH
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

    void LightBVH::queryFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const
    {
        results.clear();

        results.reserve(staticLightEntities.size() + dynamicLightEntities.size());

        if (staticBVH.isBuilt())
        {
            staticBVH.queryFrustumAppend(frustum, results);
        }

        if (dynamicBVH.isBuilt())
        {
            dynamicBVH.queryFrustumAppend(frustum, results);
        }
    }

    void LightBVH::clear()
    {
        staticBVH.clear();
        dynamicBVH.clear();
        staticLightEntities.clear();
        dynamicLightEntities.clear();
        dirtyDynamicLights.clear();
        staticDirty = true;
        dynamicDirty = true;
        staticStructuralChange = true;
        dynamicStructuralChange = true;
    }

    math::AABB LightBVH::computeLightBounds(entt::entity entity, entt::registry& registry)
    {
        // Get world transform for position/direction
        if (!registry.all_of<components::WorldTransformComponent>(entity))
        {
            return math::AABB();  // Invalid AABB
        }

        const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
        glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

        // Extract forward direction from world matrix (negative Z axis in OpenGL convention)
        glm::vec3 direction = -glm::normalize(glm::vec3(worldTransform.worldMatrix[2]));

        // Check which light type this entity has and compute appropriate bounds
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

        if (registry.all_of<components::DirectionalLightComponent>(entity))
        {
            // Directional lights affect everything - use infinite bounds
            return math::LightBounds::computeDirectionalLightAABB();
        }

        return math::AABB();  // No light component found
    }

    void LightBVH::collectStaticLightPrimitives(std::vector<math::BVHPrimitive>& primitives)
    {
        auto& registry = EntityRegistry::getRegistry();

        // Collect static point lights
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

        // Collect static spot lights
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

        // Collect static directional lights
        {
            auto view = registry.view<components::DirectionalLightComponent,
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

        // Collect dynamic point lights
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

        // Collect dynamic spot lights
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

        // Collect dynamic directional lights
        {
            auto view = registry.view<components::DirectionalLightComponent,
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
}
