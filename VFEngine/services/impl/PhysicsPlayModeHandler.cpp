#include "PhysicsPlayModeHandler.hpp"
#include "scene/WaterService.hpp"
#include "../events/EditorModeEvents.hpp"
#include "../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "components/ControllerComponents.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/EditorLogger.hpp"
#include <glm/gtc/quaternion.hpp>

namespace services
{
    namespace
    {
        void applyScaleToCollider(ColliderData& colData, const glm::vec3& scale)
        {
            // Use absolute scale to handle negative scaling
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
                // Capsule is vertical: radius scales by horizontal, height by vertical
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

            applyScaleToCollider(colData, transform.scale);

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

            applyScaleToCollider(colData, transform.scale);

            EntityHandle handle = internal::toHandle(entity);

            physicsProvider->addRigidBody(handle, rbData, colData);
            physicsProvider->setPosition(handle, transform.position);

            glm::vec3 eulerRad = glm::radians(transform.rotation);
            glm::quat rotQuat = glm::quat(eulerRad);
            physicsProvider->setRotation(handle, rotQuat);

            activePhysicsBodies.insert(handle);
        }

        initializePhysicsAnimations();
        initializeCharacterControllers();

        physicsActive = true;
        vfLogInfo("Physics play mode started with {} bodies, {} characters",
                  activePhysicsBodies.size(), activeCharacterControllers.size());
    }

    void PhysicsPlayModeHandler::exitPlayMode()
    {
        if (!physicsProvider)
        {
            return;
        }

        cleanupPhysicsAnimations();
        cleanupCharacterControllers();

        if (waterService)
        {
            waterService->clearBuoyancyTracking();
        }

        for (const auto& handle : activePhysicsBodies)
        {
            physicsProvider->removeRigidBody(handle);
        }

        activePhysicsBodies.clear();
        rootMotionLastSyncPos.clear();
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
            waterService->updateBuoyancy();
        }

        physicsProvider->update(deltaTime);
        physicsProvider->updatePhysicsAnimations(deltaTime);
        syncTransformsFromPhysics();
    }

    void PhysicsPlayModeHandler::syncTransformsFromPhysics()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        for (const auto& handle : activePhysicsBodies)
        {
            auto entity = internal::fromHandle(handle);

            if (!registry.valid(entity))
                continue;

            // Standalone colliders (no RigidBody) are static — nothing to sync
            if (!registry.all_of<components::RigidBodyComponent>(entity))
                continue;

            const auto& rigidBody = registry.get<components::RigidBodyComponent>(entity);
            if (rigidBody.type == components::RigidBodyType::Static)
                continue;

            if (registry.all_of<components::AnimatorComponent>(entity))
            {
                const auto& animComp = registry.get<components::AnimatorComponent>(entity);
                if (animComp.applyRootMotion)
                {
                    syncRootMotionEntity(handle);
                    continue;
                }
            }

            syncStandardPhysicsEntity(handle);
        }
    }

    void PhysicsPlayModeHandler::syncRootMotionEntity(EntityHandle handle)
    {
        auto entity = internal::fromHandle(handle);
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& transform = registry.get<components::TransformComponent>(entity);

        glm::vec3 physPos = physicsProvider->getPosition(handle);

        auto it = rootMotionLastSyncPos.find(handle);
        if (it != rootMotionLastSyncPos.end())
        {
            // Physics delta = how physics moved the body (gravity, collisions)
            glm::vec3 physicsDelta = physPos - it->second;
            transform.position += physicsDelta;
        }

        rootMotionLastSyncPos[handle] = transform.position;
        physicsProvider->setPosition(handle, transform.position);

        glm::vec3 eulerRad = glm::radians(transform.rotation);
        glm::quat rotQuat = glm::quat(eulerRad);
        physicsProvider->setRotation(handle, rotQuat);

        transform.isDirty = true;
    }

    void PhysicsPlayModeHandler::syncStandardPhysicsEntity(EntityHandle handle)
    {
        auto entity = internal::fromHandle(handle);
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& transform = registry.get<components::TransformComponent>(entity);
        const auto& rigidBody = registry.get<components::RigidBodyComponent>(entity);

        bool allPositionFrozen = rigidBody.freezePositionX && rigidBody.freezePositionY && rigidBody.freezePositionZ;
        bool allRotationFrozen = rigidBody.freezeRotationX && rigidBody.freezeRotationY && rigidBody.freezeRotationZ;

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

    void PhysicsPlayModeHandler::initializePhysicsAnimations()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::PhysicsAnimationComponent,
                                   components::MeshComponent,
                                   components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);
            auto& physAnimComp = view.get<components::PhysicsAnimationComponent>(entity);

            if (meshComp.meshPath.empty() || meshComp.animatorPath.empty())
            {
                continue;
            }

            auto stream = resource::MeshStreamResource::openStream(meshComp.meshPath);
            if (!stream || !stream->hasSkeletonData())
            {
                continue;
            }

            resource::SkeletonData skeletonData;
            if (!stream->readSkeleton(skeletonData) || skeletonData.bones.empty())
            {
                continue;
            }

            EntityHandle handle = internal::toHandle(entity);

            glm::vec3 eulerRad = glm::radians(transform.rotation);
            glm::quat rotQuat = glm::quat(eulerRad);

            bool created = physicsProvider->createPhysicsAnimation(
                handle, physAnimComp.config, skeletonData, transform.position, rotQuat);

            if (!created)
            {
                std::string entityName = "Unknown";
                if (registry.all_of<components::NameComponent>(entity))
                {
                    entityName = registry.get<components::NameComponent>(entity).name;
                }
                vfLogWarning("Failed to create physics animation for entity '{}'", entityName);
                continue;
            }

            if (physAnimComp.config.defaultMode == types::PhysicsAnimationMode::Ragdoll)
            {
                physicsProvider->activateRagdoll(handle);
                physAnimComp.currentMode = types::PhysicsAnimationMode::Ragdoll;
            }
            else
            {
                physicsProvider->createKinematicBones(handle, transform.position);
                physAnimComp.currentMode = physAnimComp.config.defaultMode;
            }

            physAnimComp.isInitialized = true;
            activePhysicsAnimationEntities.insert(handle);
        }

        if (!activePhysicsAnimationEntities.empty())
        {
            vfLogInfo("Initialized {} physics animation entities", activePhysicsAnimationEntities.size());
        }
    }

    void PhysicsPlayModeHandler::cleanupPhysicsAnimations()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        for (const auto& handle : activePhysicsAnimationEntities)
        {
            physicsProvider->destroyPhysicsAnimation(handle);

            auto entity = internal::fromHandle(handle);
            if (registry.valid(entity) && registry.all_of<components::PhysicsAnimationComponent>(entity))
            {
                auto& physAnimComp = registry.get<components::PhysicsAnimationComponent>(entity);
                physAnimComp.isInitialized = false;
                physAnimComp.currentMode = types::PhysicsAnimationMode::Animated;
                physAnimComp.overrideBoneMatrices.clear();
                physAnimComp.transitionProgress = 0.0f;
            }
        }

        activePhysicsAnimationEntities.clear();
    }

    void PhysicsPlayModeHandler::initializeCharacterControllers()
    {
        if (!physicsProvider) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::ControllerComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            auto& controller = view.get<components::ControllerComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            IPhysicsProvider::CharacterControllerInfo info;
            info.capsuleRadius = controller.capsuleRadius;
            info.capsuleHeight = controller.capsuleHeight;
            info.maxSlopeAngle = controller.maxSlopeAngle;
            info.stepHeight = controller.stepHeight;

            glm::vec3 eulerRad = glm::radians(transform.rotation);
            glm::quat rotQuat = glm::quat(eulerRad);

            EntityHandle handle = internal::toHandle(entity);

            if (physicsProvider->addCharacterController(handle, info, transform.position, rotQuat))
            {
                activeCharacterControllers.insert(handle);

                // Reset runtime state
                controller.isGrounded = false;
                controller.currentVelocity = glm::vec3(0.0f);
                controller.currentSpeed = 0.0f;
                controller.verticalVelocity = 0.0f;
                controller.locomotionState = components::LocomotionState::Idle;
                controller.joltCharacter = reinterpret_cast<void*>(1); // Non-null marker indicating active
            }
        }

        if (!activeCharacterControllers.empty())
        {
            vfLogInfo("Initialized {} character controllers", activeCharacterControllers.size());
        }
    }

    void PhysicsPlayModeHandler::cleanupCharacterControllers()
    {
        if (!physicsProvider) return;

        auto& registry = scene::EntityRegistry::getRegistry();

        for (const auto& handle : activeCharacterControllers)
        {
            physicsProvider->removeCharacterController(handle);

            auto entity = internal::fromHandle(handle);
            if (registry.valid(entity) && registry.all_of<components::ControllerComponent>(entity))
            {
                auto& controller = registry.get<components::ControllerComponent>(entity);
                controller.joltCharacter = nullptr;
                controller.isGrounded = false;
                controller.currentVelocity = glm::vec3(0.0f);
                controller.currentSpeed = 0.0f;
                controller.verticalVelocity = 0.0f;
                controller.locomotionState = components::LocomotionState::Idle;
            }
        }

        activeCharacterControllers.clear();
    }
}
