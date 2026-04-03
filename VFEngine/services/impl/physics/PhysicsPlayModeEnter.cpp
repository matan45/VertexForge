#include "print/Log.hpp"
#include "PhysicsPlayModeHandler.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "components/ControllerComponents.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "threading/JobSystem.hpp"
#include <glm/gtc/quaternion.hpp>

namespace services
{
    namespace
    {
        void applyScaleToCollider(ColliderData& colData, const glm::vec3& scale)
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

        std::string validateCollider(const components::ColliderComponent& collider,
                                     const components::RigidBodyComponent& rigidBody,
                                     const std::string& entityName)
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
                if (!collider.meshRef.isValid())
                    return fmt::format("Entity '{}': ConvexMesh collider has no mesh path specified", entityName);
                break;
            case components::ColliderShape::TriangleMesh:
                if (!collider.meshRef.isValid())
                    return fmt::format("Entity '{}': TriangleMesh collider has no mesh path specified", entityName);
                if (rigidBody.type == components::RigidBodyType::Dynamic)
                    return fmt::format("Entity '{}': TriangleMesh collider cannot be used with Dynamic rigid body (use Static or Kinematic)", entityName);
                break;
            }
            return "";
        }

        ColliderData buildColliderData(const components::ColliderComponent& collider, entt::entity entity,
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
            return colData;
        }
    }

    void PhysicsPlayModeHandler::enterPlayMode()
    {
        if (!physicsProvider || !physicsProvider->isInitialized())
        {
            vfLogWarning("Physics provider not available, skipping physics initialization");
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::RigidBodyComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& rigidBody = view.get<components::RigidBodyComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

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

            ColliderData colData;
            if (registry.all_of<components::ColliderComponent>(entity))
            {
                const auto& collider = registry.get<components::ColliderComponent>(entity);
                std::string entityName = registry.all_of<components::NameComponent>(entity)
                    ? registry.get<components::NameComponent>(entity).name : "Unknown";

                std::string validationError = validateCollider(collider, rigidBody, entityName);
                if (!validationError.empty())
                {
                    vfLogWarning("{} - skipping physics body creation", validationError);
                    continue;
                }
                colData = buildColliderData(collider, entity, registry);
            }
            else
            {
                colData.shape = ColliderData::Shape::Box;
                colData.size = glm::vec3(1.0f);
            }

            applyScaleToCollider(colData, transform.scale);

            EntityHandle handle = internal::toHandle(entity);
            physicsProvider->addRigidBody(handle, rbData, colData);
            physicsProvider->setPosition(handle, transform.position);

            glm::quat rotQuat = glm::quat(glm::radians(transform.rotation));
            physicsProvider->setRotation(handle, rotQuat);
            activePhysicsBodies.insert(handle);

        }

        auto colliderOnlyView = registry.view<components::ColliderComponent, components::TransformComponent>(
            entt::exclude<components::RigidBodyComponent>);

        for (auto entity : colliderOnlyView)
        {
            const auto& collider = colliderOnlyView.get<components::ColliderComponent>(entity);
            const auto& transform = colliderOnlyView.get<components::TransformComponent>(entity);

            RigidBodyData rbData;
            rbData.type = RigidBodyData::Type::Static;

            ColliderData colData = buildColliderData(collider, entity, registry);
            applyScaleToCollider(colData, transform.scale);

            EntityHandle handle = internal::toHandle(entity);
            physicsProvider->addRigidBody(handle, rbData, colData);
            physicsProvider->setPosition(handle, transform.position);

            glm::quat rotQuat = glm::quat(glm::radians(transform.rotation));
            physicsProvider->setRotation(handle, rotQuat);
            activePhysicsBodies.insert(handle);
        }

        initializePhysicsAnimations();
        initializeCharacterControllers();

        if (scriptFixedUpdateCallback)
        {
            physicsProvider->setPostStepCallback([this](float fixedDt)
            {
                scriptFixedUpdateCallback(fixedDt);
            });
        }

        physicsActive = true;
        vfLogInfo("Physics play mode started with {} bodies, {} characters",
                  activePhysicsBodies.size(), activeCharacterControllers.size());
    }
}
