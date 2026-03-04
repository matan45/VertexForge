#include "WaterService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"

namespace services
{
    void WaterService::updateBuoyancy()
    {
        if (!physicsProvider || entitiesInWater.empty() || waterGrids.empty())
            return;

        auto settings = getWaterGlobalSettings();
        glm::vec3 gravity = physicsProvider->getGravity();
        float gravityMag = glm::length(gravity);

        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto it = entitiesInWater.begin(); it != entitiesInWater.end(); )
        {
            EntityHandle entity = *it;
            entt::entity ent = internal::fromHandle(entity);

            if (!registry.valid(ent) || !physicsProvider->hasRigidBody(entity))
            {
                it = entitiesInWater.erase(it);
                continue;
            }

            if (registry.all_of<components::RigidBodyComponent>(ent))
            {
                const auto& rb = registry.get<components::RigidBodyComponent>(ent);
                if (rb.type == components::RigidBodyType::Static)
                {
                    ++it;
                    continue;
                }
            }

            glm::vec3 pos = physicsProvider->getPosition(entity);

            float halfHeight = 0.5f;
            if (registry.all_of<components::ColliderComponent>(ent))
            {
                const auto& collider = registry.get<components::ColliderComponent>(ent);
                switch (collider.shape)
                {
                case components::ColliderShape::Box:
                    halfHeight = collider.size.y;
                    break;
                case components::ColliderShape::Sphere:
                    halfHeight = collider.size.x;
                    break;
                case components::ColliderShape::Capsule:
                    halfHeight = collider.size.x + collider.height * 0.5f;
                    break;
                default:
                    halfHeight = 0.5f;
                    break;
                }
            }

            float waterHeight = getWaterHeightAt(glm::vec2(pos.x, pos.z));
            float objectBottom = pos.y - halfHeight;
            float objectHeight = halfHeight * 2.0f;

            float submergedDepth = glm::clamp(waterHeight - objectBottom, 0.0f, objectHeight);
            float submersionRatio = submergedDepth / objectHeight;

            if (submersionRatio <= 0.0f)
            {
                ++it;
                continue;
            }

            float mass = 1.0f;
            if (registry.all_of<components::RigidBodyComponent>(ent))
            {
                mass = registry.get<components::RigidBodyComponent>(ent).mass;
            }

            // Mass-proportional buoyancy: at equilibrium submersionRatio = 1/buoyancyStrength
            float buoyancyForce = mass * gravityMag * submersionRatio * settings.buoyancyStrength;
            physicsProvider->applyForce(entity, glm::vec3(0.0f, buoyancyForce, 0.0f));

            // Drag: opposes velocity proportional to submersion
            glm::vec3 velocity = physicsProvider->getLinearVelocity(entity);
            glm::vec3 dragForce = -velocity * settings.drag * submersionRatio * mass;
            physicsProvider->applyForce(entity, dragForce);

            ++it;
        }
    }

    void WaterService::clearBuoyancyTracking()
    {
        entitiesInWater.clear();
    }
}
