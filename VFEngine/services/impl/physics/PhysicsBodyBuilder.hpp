#pragma once

// Shared helpers that build service-layer physics data (ColliderData / RigidBodyData)
// from ECS components. Used by the play-mode-enter path (PhysicsPlayModeEnter.cpp) and the
// runtime body-creation command path (PhysicsServiceImpl.cpp) so the component->data mapping
// lives in exactly one place. (VK-1351)

#include "../../interfaces/physics/IPhysicsService.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

namespace services
{
    inline void applyScaleToCollider(ColliderData& colData, const glm::vec3& scale)
    {
        glm::vec3 absScale = glm::abs(scale);

        switch (colData.shape)
        {
        case ColliderData::Shape::Box:
            colData.size *= absScale;
            break;
        case ColliderData::Shape::Sphere:
        {
            float uniformScale = glm::max(absScale.x, glm::max(absScale.y, absScale.z));
            colData.size.x *= uniformScale;
            break;
        }
        case ColliderData::Shape::Capsule:
        {
            float horizontalScale = glm::max(absScale.x, absScale.z);
            colData.size.x *= horizontalScale;
            colData.height *= absScale.y;
            break;
        }
        case ColliderData::Shape::ConvexMesh:
        case ColliderData::Shape::TriangleMesh:
            colData.size *= absScale;
            break;
        }

        colData.offset *= absScale;
    }

    inline bool hasMeshColliderSource(const components::ColliderComponent& collider,
                                      const entt::registry* registry = nullptr,
                                      entt::entity entity = entt::null)
    {
        if (collider.meshRef.isValid())
            return true;

        return registry != nullptr
            && entity != entt::null
            && registry->all_of<components::MeshComponent>(entity)
            && registry->get<components::MeshComponent>(entity).meshRef.isValid();
    }

    inline std::string validateCollider(const components::ColliderComponent& collider,
                                        const components::RigidBodyComponent& rigidBody,
                                        const std::string& entityName,
                                        const entt::registry* registry = nullptr,
                                        entt::entity entity = entt::null)
    {
        switch (collider.shape)
        {
        case components::ColliderShape::Box:
            if (collider.size.x <= 0.0f || collider.size.y <= 0.0f || collider.size.z <= 0.0f)
                return fmt::format("Entity '{}': Box collider has invalid dimensions ({}, {}, {})",
                    entityName, collider.size.x, collider.size.y, collider.size.z);
            break;
        case components::ColliderShape::Sphere:
            if (collider.size.x <= 0.0f)
                return fmt::format("Entity '{}': Sphere collider has invalid radius ({})",
                    entityName, collider.size.x);
            break;
        case components::ColliderShape::Capsule:
            if (collider.size.x <= 0.0f)
                return fmt::format("Entity '{}': Capsule collider has invalid radius ({})",
                    entityName, collider.size.x);
            if (collider.height <= 0.0f)
                return fmt::format("Entity '{}': Capsule collider has invalid height ({})",
                    entityName, collider.height);
            break;
        case components::ColliderShape::ConvexMesh:
            if (!hasMeshColliderSource(collider, registry, entity))
                return fmt::format("Entity '{}': ConvexMesh collider has no mesh path specified", entityName);
            break;
        case components::ColliderShape::TriangleMesh:
            if (!hasMeshColliderSource(collider, registry, entity))
                return fmt::format("Entity '{}': TriangleMesh collider has no mesh path specified", entityName);
            if (rigidBody.type == components::RigidBodyType::Dynamic)
                return fmt::format("Entity '{}': TriangleMesh collider cannot be used with Dynamic rigid body (use Static or Kinematic)", entityName);
            break;
        }
        return "";
    }

    inline ColliderData buildColliderData(const components::ColliderComponent& collider, entt::entity entity,
                                          const entt::registry& registry)
    {
        ColliderData colData;
        switch (collider.shape)
        {
        case components::ColliderShape::Box:          colData.shape = ColliderData::Shape::Box; break;
        case components::ColliderShape::Sphere:       colData.shape = ColliderData::Shape::Sphere; break;
        case components::ColliderShape::Capsule:      colData.shape = ColliderData::Shape::Capsule; break;
        case components::ColliderShape::ConvexMesh:
            colData.shape = ColliderData::Shape::ConvexMesh;
            if (collider.meshRef.isValid())
                colData.meshPath = collider.meshRef.resolve();
            else if (registry.all_of<components::MeshComponent>(entity))
                colData.meshPath = registry.get<components::MeshComponent>(entity).meshRef.resolve();
            break;
        case components::ColliderShape::TriangleMesh:
            colData.shape = ColliderData::Shape::TriangleMesh;
            if (collider.meshRef.isValid())
                colData.meshPath = collider.meshRef.resolve();
            else if (registry.all_of<components::MeshComponent>(entity))
                colData.meshPath = registry.get<components::MeshComponent>(entity).meshRef.resolve();
            break;
        }
        colData.size = collider.size;
        colData.height = collider.height;
        colData.isTrigger = collider.isTrigger;
        colData.offset = collider.offset;
        colData.collisionLayer = collider.collisionLayer;
        colData.submeshIndex = collider.submeshIndex;
        return colData;
    }

    inline RigidBodyData buildRigidBodyData(const components::RigidBodyComponent& rigidBody)
    {
        RigidBodyData rbData;
        switch (rigidBody.type)
        {
        case components::RigidBodyType::Static:    rbData.type = RigidBodyData::Type::Static; break;
        case components::RigidBodyType::Dynamic:   rbData.type = RigidBodyData::Type::Dynamic; break;
        case components::RigidBodyType::Kinematic: rbData.type = RigidBodyData::Type::Kinematic; break;
        }
        rbData.mass = rigidBody.mass;
        rbData.linearDamping = rigidBody.linearDamping;
        rbData.angularDamping = rigidBody.angularDamping;
        return rbData;
    }
}
