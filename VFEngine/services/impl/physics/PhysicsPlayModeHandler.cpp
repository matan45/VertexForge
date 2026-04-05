#include "print/Log.hpp"
#include "PhysicsPlayModeHandler.hpp"
#include "../scene/OceanService.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
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

        rigidBodyAddedToken = dispatcher.subscribe<::events::physics::RigidBodyAddedNotification>(
            [this](const ::events::physics::RigidBodyAddedNotification& notification)
            {
                if (physicsActive)
                {
                    activePhysicsBodies.insert(notification.entity);
                }
            });

        rigidBodyRemovedToken = dispatcher.subscribe<::events::physics::RigidBodyRemovedNotification>(
            [this](const ::events::physics::RigidBodyRemovedNotification& notification)
            {
                activePhysicsBodies.erase(notification.entity);
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
        if (rigidBodyAddedToken.isValid())
        {
            dispatcher.unsubscribe(rigidBodyAddedToken);
            rigidBodyAddedToken = {};
        }
        if (rigidBodyRemovedToken.isValid())
        {
            dispatcher.unsubscribe(rigidBodyRemovedToken);
            rigidBodyRemovedToken = {};
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

    void PhysicsPlayModeHandler::exitPlayMode()
    {
        if (!physicsProvider)
        {
            return;
        }

        cleanupPhysicsAnimations();
        cleanupCharacterControllers();

        if (oceanService)
        {
            oceanService->clearBuoyancyTracking();
        }

        for (const auto& handle : activePhysicsBodies)
        {
            physicsProvider->removeRigidBody(handle);
        }

        physicsProvider->setPostStepCallback(nullptr);

        activePhysicsBodies.clear();
        rootMotionLastSyncPos.clear();
        physicsActive = false;

        vfLogInfo("Physics play mode stopped");
    }

    void PhysicsPlayModeHandler::update(float deltaTime)
    {
        kickUpdate(deltaTime);
        syncUpdate(deltaTime);
    }

    void PhysicsPlayModeHandler::kickUpdate(float deltaTime)
    {
        if (!physicsActive || !physicsProvider)
        {
            return;
        }

        if (oceanService)
        {
            oceanService->updateBuoyancy();
        }

        physicsProvider->kickPhysicsStep(deltaTime);
    }

    void PhysicsPlayModeHandler::syncUpdate(float deltaTime)
    {
        if (!physicsActive || !physicsProvider)
        {
            return;
        }

        physicsProvider->syncPhysicsStep();
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

        auto snapshot = physicsProvider->getInterpolatedTransform(handle);

        bool allPositionFrozen = rigidBody.freezePositionX && rigidBody.freezePositionY && rigidBody.freezePositionZ;
        bool allRotationFrozen = rigidBody.freezeRotationX && rigidBody.freezeRotationY && rigidBody.freezeRotationZ;

        if (!allPositionFrozen)
        {
            glm::vec3 physPos = snapshot.position;

            if (rigidBody.freezePositionX) physPos.x = transform.position.x;
            if (rigidBody.freezePositionY) physPos.y = transform.position.y;
            if (rigidBody.freezePositionZ) physPos.z = transform.position.z;

            transform.position = physPos;
        }

        if (!allRotationFrozen)
        {
            glm::quat physRot = snapshot.rotation;
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

        // Pre-load all skeleton data in parallel (I/O heavy)
        struct PhysAnimEntry
        {
            entt::entity entity;
            std::string meshPath;
        };

        std::vector<PhysAnimEntry> entries;
        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);
            if (!meshComp.meshRef.isValid() || !meshComp.animatorRef.isValid())
                continue;
            entries.push_back({entity, meshComp.meshRef.resolve()});
        }

        // Phase 1: parallel skeleton loading
        std::vector<std::future<std::shared_ptr<resource::SkeletonData>>> skeletonFutures;
        skeletonFutures.reserve(entries.size());

        for (const auto& entry : entries)
        {
            skeletonFutures.push_back(threading::JobSystem::instance().submit(
                [meshPath = entry.meshPath]() -> std::shared_ptr<resource::SkeletonData>
                {
                    auto stream = resource::MeshStreamResource::openStream(meshPath);
                    if (!stream || !stream->hasSkeletonData())
                        return nullptr;

                    auto skeletonData = std::make_shared<resource::SkeletonData>();
                    if (!stream->readSkeleton(*skeletonData) || skeletonData->bones.empty())
                        return nullptr;

                    return skeletonData;
                }, threading::JobPriority::NORMAL
            ));
        }

        // Phase 2: sequential physics provider calls
        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto skeletonData = skeletonFutures[i].get();
            if (!skeletonData)
                continue;

            auto entity = entries[i].entity;
            const auto& transform = view.get<components::TransformComponent>(entity);
            auto& physAnimComp = view.get<components::PhysicsAnimationComponent>(entity);

            EntityHandle handle = internal::toHandle(entity);

            glm::vec3 eulerRad = glm::radians(transform.rotation);
            glm::quat rotQuat = glm::quat(eulerRad);

            bool created = physicsProvider->createPhysicsAnimation(
                handle, physAnimComp.config, *skeletonData, transform.position, rotQuat);

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
        auto view = registry.view<components::ControllerComponent, components::TransformComponent,
                                   components::ColliderComponent>();

        for (auto entity : view)
        {
            auto& controller = view.get<components::ControllerComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);
            const auto& collider = view.get<components::ColliderComponent>(entity);

            IPhysicsProvider::CharacterControllerInfo info;
            info.shape = collider.shape;
            info.size = collider.size;
            info.maxSlopeAngle = controller.maxSlopeAngle;
            info.stepHeight = controller.stepHeight;
            info.collisionLayer = collider.collisionLayer;

            glm::vec3 eulerRad = glm::radians(transform.rotation);
            glm::quat rotQuat = glm::quat(eulerRad);

            EntityHandle handle = internal::toHandle(entity);

            if (physicsProvider->addCharacterController(handle, info, transform.position, rotQuat))
            {
                activeCharacterControllers.insert(handle);

                controller.isGrounded = false;
                controller.currentVelocity = glm::vec3(0.0f);
                controller.currentSpeed = 0.0f;
                controller.verticalVelocity = 0.0f;
                controller.locomotionState = "Idle";
                controller.characterControllerActive = true;
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
                controller.characterControllerActive = false;
                controller.isGrounded = false;
                controller.currentVelocity = glm::vec3(0.0f);
                controller.currentSpeed = 0.0f;
                controller.verticalVelocity = 0.0f;
                controller.locomotionState = "Idle";
            }
        }

        activeCharacterControllers.clear();
    }
}
