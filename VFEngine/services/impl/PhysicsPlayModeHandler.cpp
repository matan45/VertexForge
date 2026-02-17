#include "PhysicsPlayModeHandler.hpp"
#include "scene/WaterService.hpp"
#include "../events/EditorModeEvents.hpp"
#include "../events/PhysicsEvents.hpp"
#include "../events/SceneEvents.hpp"
#include "../data/EditorMode.hpp"
#include "../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/EditorLogger.hpp"
#include <glm/gtc/quaternion.hpp>

namespace services
{
    namespace
    {
        std::string validateCollider(const components::ColliderComponent& collider,
                                     const components::RigidBodyComponent& rigidBody,
                                     const std::string& entityName)
        {
            switch (collider.shape)
            {
            case components::ColliderShape::Box:
                if (collider.size.x <= 0.0f || collider.size.y <= 0.0f || collider.size.z <= 0.0f)
                {
                    return fmt::format("Entity '{}': Box collider has invalid dimensions ({}, {}, {})",
                        entityName, collider.size.x, collider.size.y, collider.size.z);
                }
                break;

            case components::ColliderShape::Sphere:
                if (collider.size.x <= 0.0f)
                {
                    return fmt::format("Entity '{}': Sphere collider has invalid radius ({})",
                        entityName, collider.size.x);
                }
                break;

            case components::ColliderShape::Capsule:
                if (collider.size.x <= 0.0f)
                {
                    return fmt::format("Entity '{}': Capsule collider has invalid radius ({})",
                        entityName, collider.size.x);
                }
                if (collider.height <= 0.0f)
                {
                    return fmt::format("Entity '{}': Capsule collider has invalid height ({})",
                        entityName, collider.height);
                }
                break;

            case components::ColliderShape::ConvexMesh:
                if (collider.meshPath.empty())
                {
                    return fmt::format("Entity '{}': ConvexMesh collider has no mesh path specified",
                        entityName);
                }
                break;

            case components::ColliderShape::TriangleMesh:
                if (collider.meshPath.empty())
                {
                    return fmt::format("Entity '{}': TriangleMesh collider has no mesh path specified",
                        entityName);
                }
                // Triangle meshes must be static - they cannot be dynamic in physics engines
                if (rigidBody.type == components::RigidBodyType::Dynamic)
                {
                    return fmt::format("Entity '{}': TriangleMesh collider cannot be used with Dynamic rigid body (use Static or Kinematic)",
                        entityName);
                }
                break;
            }

            return "";
        }
    }
    PhysicsPlayModeHandler::PhysicsPlayModeHandler(IPhysicsProvider* physicsProvider)
        : physicsProvider(physicsProvider)
    {
    }

    PhysicsPlayModeHandler::~PhysicsPlayModeHandler()
    {
        unsubscribeFromEvents();
    }

    void PhysicsPlayModeHandler::subscribeToEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        editorModeChangedToken = dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& notification)
            {
                onEditorModeChanged(notification.previousMode, notification.currentMode);
            });
    }

    void PhysicsPlayModeHandler::unsubscribeFromEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (editorModeChangedToken.isValid())
        {
            dispatcher.unsubscribe(editorModeChangedToken);
            editorModeChangedToken = {};
        }
    }

    void PhysicsPlayModeHandler::onEditorModeChanged(EditorMode previousMode, EditorMode currentMode)
    {
        if (previousMode == EditorMode::Edit && currentMode == EditorMode::Play)
        {
            enterPlayMode();
        }
        else if (previousMode == EditorMode::Play && currentMode == EditorMode::Edit)
        {
            exitPlayMode();
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
            case components::RigidBodyType::Static:
                rbData.type = RigidBodyData::Type::Static;
                break;
            case components::RigidBodyType::Dynamic:
                rbData.type = RigidBodyData::Type::Dynamic;
                break;
            case components::RigidBodyType::Kinematic:
                rbData.type = RigidBodyData::Type::Kinematic;
                break;
            }
            rbData.mass = rigidBody.mass;
            rbData.linearDamping = rigidBody.linearDamping;
            rbData.angularDamping = rigidBody.angularDamping;
            rbData.linearVelocity = glm::vec3(0.0f);
            rbData.angularVelocity = glm::vec3(0.0f);

            ColliderData colData;
            bool hasCollider = registry.all_of<components::ColliderComponent>(entity);

            if (hasCollider)
            {
                const auto& collider = registry.get<components::ColliderComponent>(entity);

                std::string entityName = "Unknown";
                if (registry.all_of<components::NameComponent>(entity))
                {
                    entityName = registry.get<components::NameComponent>(entity).name;
                }

                std::string validationError = validateCollider(collider, rigidBody, entityName);
                if (!validationError.empty())
                {
                    vfLogWarning("{} - skipping physics body creation", validationError);
                    continue;
                }

                switch (collider.shape)
                {
                case components::ColliderShape::Box:
                    colData.shape = ColliderData::Shape::Box;
                    break;
                case components::ColliderShape::Sphere:
                    colData.shape = ColliderData::Shape::Sphere;
                    break;
                case components::ColliderShape::Capsule:
                    colData.shape = ColliderData::Shape::Capsule;
                    break;
                case components::ColliderShape::ConvexMesh:
                    colData.shape = ColliderData::Shape::ConvexMesh;
                    if (!collider.meshPath.empty())
                    {
                        colData.meshPath = collider.meshPath;
                    }
                    else if (registry.all_of<components::MeshComponent>(entity))
                    {
                        colData.meshPath = registry.get<components::MeshComponent>(entity).meshPath;
                    }
                    break;
                case components::ColliderShape::TriangleMesh:
                    colData.shape = ColliderData::Shape::TriangleMesh;
                    if (!collider.meshPath.empty())
                    {
                        colData.meshPath = collider.meshPath;
                    }
                    else if (registry.all_of<components::MeshComponent>(entity))
                    {
                        colData.meshPath = registry.get<components::MeshComponent>(entity).meshPath;
                    }
                    break;
                }
                colData.size = collider.size;
                colData.height = collider.height;
                colData.isTrigger = collider.isTrigger;
                colData.offset = collider.offset;
                colData.collisionLayer = collider.collisionLayer;
            }
            else
            {
                colData.shape = ColliderData::Shape::Box;
                colData.size = glm::vec3(1.0f);
            }

            EntityHandle handle = internal::toHandle(entity);

            physicsProvider->addRigidBody(handle, rbData, colData);
            physicsProvider->setPosition(handle, transform.position);

            glm::vec3 eulerRad = glm::radians(transform.rotation);
            glm::quat rotQuat = glm::quat(eulerRad);
            physicsProvider->setRotation(handle, rotQuat);

            activePhysicsBodies.insert(handle);
        }

        // Also add entities with ColliderComponent but no RigidBodyComponent as static bodies
        auto colliderOnlyView = registry.view<components::ColliderComponent, components::TransformComponent>(
            entt::exclude<components::RigidBodyComponent>);

        for (auto entity : colliderOnlyView)
        {
            const auto& collider = colliderOnlyView.get<components::ColliderComponent>(entity);
            const auto& transform = colliderOnlyView.get<components::TransformComponent>(entity);

            std::string entityName = "Unknown";
            if (registry.all_of<components::NameComponent>(entity))
            {
                entityName = registry.get<components::NameComponent>(entity).name;
            }

            // Create a static rigid body for standalone colliders
            RigidBodyData rbData;
            rbData.type = RigidBodyData::Type::Static;
            rbData.mass = 0.0f;
            rbData.linearDamping = 0.0f;
            rbData.angularDamping = 0.0f;

            ColliderData colData;
            switch (collider.shape)
            {
            case components::ColliderShape::Box:
                colData.shape = ColliderData::Shape::Box;
                break;
            case components::ColliderShape::Sphere:
                colData.shape = ColliderData::Shape::Sphere;
                break;
            case components::ColliderShape::Capsule:
                colData.shape = ColliderData::Shape::Capsule;
                break;
            case components::ColliderShape::ConvexMesh:
                colData.shape = ColliderData::Shape::ConvexMesh;
                if (!collider.meshPath.empty())
                {
                    colData.meshPath = collider.meshPath;
                }
                else if (registry.all_of<components::MeshComponent>(entity))
                {
                    colData.meshPath = registry.get<components::MeshComponent>(entity).meshPath;
                }
                break;
            case components::ColliderShape::TriangleMesh:
                colData.shape = ColliderData::Shape::TriangleMesh;
                if (!collider.meshPath.empty())
                {
                    colData.meshPath = collider.meshPath;
                }
                else if (registry.all_of<components::MeshComponent>(entity))
                {
                    colData.meshPath = registry.get<components::MeshComponent>(entity).meshPath;
                }
                break;
            }
            colData.size = collider.size;
            colData.height = collider.height;
            colData.isTrigger = collider.isTrigger;
            colData.offset = collider.offset;
            colData.collisionLayer = collider.collisionLayer;

            EntityHandle handle = internal::toHandle(entity);

            physicsProvider->addRigidBody(handle, rbData, colData);
            physicsProvider->setPosition(handle, transform.position);

            glm::vec3 eulerRad = glm::radians(transform.rotation);
            glm::quat rotQuat = glm::quat(eulerRad);
            physicsProvider->setRotation(handle, rotQuat);

            activePhysicsBodies.insert(handle);
        }

        physicsActive = true;
        vfLogInfo("Physics play mode started with {} bodies", activePhysicsBodies.size());
    }

    void PhysicsPlayModeHandler::exitPlayMode()
    {
        if (!physicsProvider)
        {
            return;
        }

        if (waterService)
        {
            waterService->clearBuoyancyTracking();
        }

        for (const auto& handle : activePhysicsBodies)
        {
            physicsProvider->removeRigidBody(handle);
        }

        activePhysicsBodies.clear();
        physicsActive = false;

        vfLogInfo("Physics play mode stopped");
    }

    void PhysicsPlayModeHandler::update(float deltaTime)
    {
        if (!physicsActive || !physicsProvider)
        {
            return;
        }

        if (waterService)
        {
            waterService->updateBuoyancy(deltaTime);
        }

        physicsProvider->update(deltaTime);
        syncTransformsFromPhysics();
    }

    void PhysicsPlayModeHandler::syncTransformsFromPhysics()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        for (const auto& handle : activePhysicsBodies)
        {
            auto entity = internal::fromHandle(handle);

            if (!registry.valid(entity))
            {
                continue;
            }

            // Skip entities without RigidBodyComponent (standalone colliders are static)
            if (!registry.all_of<components::RigidBodyComponent>(entity))
            {
                continue;
            }

            auto& rigidBody = registry.get<components::RigidBodyComponent>(entity);

            if (rigidBody.type == components::RigidBodyType::Static)
            {
                continue;
            }

            bool allPositionFrozen = rigidBody.freezePositionX && rigidBody.freezePositionY && rigidBody.freezePositionZ;
            bool allRotationFrozen = rigidBody.freezeRotationX && rigidBody.freezeRotationY && rigidBody.freezeRotationZ;

            auto& transform = registry.get<components::TransformComponent>(entity);

            if (!allPositionFrozen)
            {
                glm::vec3 physPos = physicsProvider->getPosition(handle);

                if (rigidBody.freezePositionX) physPos.x = transform.position.x;
                if (rigidBody.freezePositionY) physPos.y = transform.position.y;
                if (rigidBody.freezePositionZ) physPos.z = transform.position.z;

                transform.position = physPos;
            }

            if (!allRotationFrozen)
            {
                glm::quat physRot = physicsProvider->getRotation(handle);
                glm::vec3 eulerRad = glm::eulerAngles(physRot);
                glm::vec3 eulerDeg = glm::degrees(eulerRad);
                transform.rotation = eulerDeg;
            }

            transform.isDirty = true;
        }
    }
}
