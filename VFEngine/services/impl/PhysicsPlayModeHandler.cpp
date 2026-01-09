#include "PhysicsPlayModeHandler.hpp"
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
        constexpr float MIN_DIMENSION = 0.001f;

        // Validates collider dimensions and settings, returns error message or empty string if valid
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

            return ""; // Valid
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

        // Find all entities with RigidBodyComponent
        auto view = registry.view<components::RigidBodyComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& rigidBody = view.get<components::RigidBodyComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            // Convert ECS component to physics data
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
            rbData.useGravity = rigidBody.useGravity;
            rbData.linearVelocity = glm::vec3(0.0f);
            rbData.angularVelocity = glm::vec3(0.0f);

            // Get collider data if entity has ColliderComponent
            ColliderData colData;
            bool hasCollider = registry.all_of<components::ColliderComponent>(entity);

            if (hasCollider)
            {
                const auto& collider = registry.get<components::ColliderComponent>(entity);

                // Get entity name for error messages
                std::string entityName = "Unknown";
                if (registry.all_of<components::NameComponent>(entity))
                {
                    entityName = registry.get<components::NameComponent>(entity).name;
                }

                // Validate collider configuration
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
                case components::ColliderShape::TriangleMesh:
                    colData.shape = ColliderData::Shape::Mesh;
                    break;
                }
                colData.size = collider.size;
                colData.height = collider.height;
                colData.isTrigger = collider.isTrigger;
                colData.offset = collider.offset;
            }
            else
            {
                // Default box collider
                colData.shape = ColliderData::Shape::Box;
                colData.size = glm::vec3(1.0f);
            }

            EntityHandle handle = internal::toHandle(entity);

            // Set initial position/rotation from transform before adding to physics
            physicsProvider->addRigidBody(handle, rbData, colData);
            physicsProvider->setPosition(handle, transform.position);

            // Convert Euler angles (degrees) to quaternion
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

        // Remove all physics bodies we created
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

        // Step the physics simulation
        physicsProvider->update(deltaTime);

        // Sync transforms from physics back to ECS
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

            auto& rigidBody = registry.get<components::RigidBodyComponent>(entity);

            // Only sync dynamic bodies (static and kinematic are controlled by other means)
            if (rigidBody.type == components::RigidBodyType::Static)
            {
                continue;
            }

            // Check constraints - if all position axes are frozen, skip position sync
            bool allPositionFrozen = rigidBody.freezePositionX && rigidBody.freezePositionY && rigidBody.freezePositionZ;
            bool allRotationFrozen = rigidBody.freezeRotationX && rigidBody.freezeRotationY && rigidBody.freezeRotationZ;

            auto& transform = registry.get<components::TransformComponent>(entity);

            if (!allPositionFrozen)
            {
                glm::vec3 physPos = physicsProvider->getPosition(handle);

                // Apply individual axis constraints
                if (rigidBody.freezePositionX) physPos.x = transform.position.x;
                if (rigidBody.freezePositionY) physPos.y = transform.position.y;
                if (rigidBody.freezePositionZ) physPos.z = transform.position.z;

                transform.position = physPos;
            }

            if (!allRotationFrozen)
            {
                glm::quat physRot = physicsProvider->getRotation(handle);
                // Convert quaternion to Euler angles (degrees)
                glm::vec3 eulerRad = glm::eulerAngles(physRot);
                glm::vec3 eulerDeg = glm::degrees(eulerRad);
                transform.rotation = eulerDeg;
            }
        }
    }
}
