#include "print/Log.hpp"
#include "PhysicsPlayModeHandler.hpp"
#include "../SceneSubtreeUtils.hpp"
#include "../scene/OceanService.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "components/ControllerComponents.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "threading/JobSystem.hpp"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

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
                if (physicsActive.load())
                {
                    activePhysicsBodies.insert(notification.entity);
                }
            });

        rigidBodyRemovedToken = dispatcher.subscribe<::events::physics::RigidBodyRemovedNotification>(
            [this](const ::events::physics::RigidBodyRemovedNotification& notification)
            {
                activePhysicsBodies.erase(notification.entity);
                activeVehicleEntities.erase(notification.entity);
            });

        // VK-1437: a prefab instantiated AFTER Play started gets no physics-animation state from the
        // one-shot Play-entry pass. Initialize its subtree here so it can immediately respond to
        // physics-animation queries/commands. Gated on physicsActive so Edit-mode instantiation is a no-op
        // (those entities are picked up by the full pass at the next Play entry).
        prefabInstantiatedToken = dispatcher.subscribe<::events::scene::PrefabInstantiatedNotification>(
            [this](const ::events::scene::PrefabInstantiatedNotification& notification)
            {
                if (!physicsActive.load() || !physicsProvider)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(pendingMutex);
                pendingPrefabRoots.push_back(notification.rootEntity);
            });

        // VK-1437: tear down provider-side physics-animation state when an entity is deleted during Play.
        // EntityDeletedNotification fires per-entity for the whole subtree before removal, so deleting a
        // prefab root cleans up every descendant ragdoll. destroyPhysicsAnimation is idempotent.
        entityDeletedToken = dispatcher.subscribe<::events::scene::EntityDeletedNotification>(
            [this](const ::events::scene::EntityDeletedNotification& notification)
            {
                if (!physicsActive.load() || !physicsProvider)
                {
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(pendingMutex);
                    pendingPrefabRoots.erase(
                        std::remove(pendingPrefabRoots.begin(), pendingPrefabRoots.end(), notification.entity),
                        pendingPrefabRoots.end());
                }

                if (activePhysicsAnimationEntities.erase(notification.entity) > 0 ||
                    physicsProvider->hasPhysicsAnimation(notification.entity))
                {
                    physicsProvider->destroyPhysicsAnimation(notification.entity);
                }

                if (activeVehicleEntities.erase(notification.entity) > 0 ||
                    physicsProvider->hasVehicle(notification.entity))
                {
                    physicsProvider->destroyVehicle(notification.entity);
                }
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
        if (prefabInstantiatedToken.isValid())
        {
            dispatcher.unsubscribe(prefabInstantiatedToken);
            prefabInstantiatedToken = {};
        }
        if (entityDeletedToken.isValid())
        {
            dispatcher.unsubscribe(entityDeletedToken);
            entityDeletedToken = {};
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
        cleanupVehicles();

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
        activeVehicleEntities.clear();
        rootMotionLastSyncPos.clear();
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingPrefabRoots.clear();
        }
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
        if (!physicsActive.load() || !physicsProvider)
        {
            return;
        }

        drainPendingPrefabRoots();

        if (oceanService)
        {
            oceanService->updateBuoyancy();
        }

        physicsProvider->kickPhysicsStep(deltaTime);
    }

    void PhysicsPlayModeHandler::syncUpdate(float deltaTime)
    {
        if (!physicsActive.load() || !physicsProvider)
        {
            return;
        }

        physicsProvider->syncPhysicsStep();
        physicsProvider->updatePhysicsAnimations(deltaTime);
        syncTransformsFromPhysics();

        // Publish water enter/exit recorded during kickUpdate — must run on the main
        // thread because script listeners react to these notifications.
        if (oceanService)
        {
            oceanService->flushWaterEvents();
        }
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

    void PhysicsPlayModeHandler::initializeVehicles()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::VehicleComponent, components::TransformComponent>();

        std::vector<EntityHandle> candidates;
        for (auto entity : view)
        {
            candidates.push_back(internal::toHandle(entity));
        }

        initializeVehiclesFor(candidates);
    }

    void PhysicsPlayModeHandler::cleanupVehicles()
    {
        if (!physicsProvider) return;

        for (const auto& handle : activeVehicleEntities)
            physicsProvider->destroyVehicle(handle);

        activeVehicleEntities.clear();
    }

    void PhysicsPlayModeHandler::initializePhysicsAnimations()
    {
        // Play-entry full pass: every physics-animation entity currently in the scene.
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::PhysicsAnimationComponent,
                                   components::MeshComponent,
                                   components::TransformComponent>();

        std::vector<EntityHandle> candidates;
        for (auto entity : view)
        {
            candidates.push_back(internal::toHandle(entity));
        }

        initializePhysicsAnimationsFor(candidates);
    }

    void PhysicsPlayModeHandler::drainPendingPrefabRoots()
    {
        std::vector<EntityHandle> roots;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pendingPrefabRoots.empty())
                return;
            roots.swap(pendingPrefabRoots);
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<EntityHandle> candidates;
        for (EntityHandle root : roots)
        {
            entt::entity rootEntity = internal::fromHandle(root);
            if (!registry.valid(rootEntity))
                continue;

            internal::collectSubtreeHandles(scene::Entity(rootEntity), candidates);
        }

        if (!candidates.empty())
        {
            initializeVehiclesFor(candidates);
            initializePhysicsAnimationsFor(candidates);
        }
    }

    void PhysicsPlayModeHandler::initializeVehiclesFor(const std::vector<EntityHandle>& candidates)
    {
        if (!physicsProvider)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        size_t createdCount = 0;

        for (const auto& handle : candidates)
        {
            if (activeVehicleEntities.contains(handle))
                continue;

            entt::entity entity = internal::fromHandle(handle);
            if (!registry.valid(entity) ||
                !registry.all_of<components::VehicleComponent, components::TransformComponent>(entity))
            {
                continue;
            }

            if (!physicsProvider->hasRigidBody(handle))
                continue;

            const auto& vehicle = registry.get<components::VehicleComponent>(entity);
            if (physicsProvider->createVehicle(handle, vehicle.config))
            {
                activeVehicleEntities.insert(handle);
                ++createdCount;
            }
            else
            {
                const std::string entityName = registry.all_of<components::NameComponent>(entity)
                    ? registry.get<components::NameComponent>(entity).name : "Unknown";
                vfLogWarning("Failed to create vehicle for entity '{}'", entityName);
            }
        }

        if (createdCount > 0)
        {
            vfLogInfo("Initialized {} vehicle entities", createdCount);
        }
    }

    void PhysicsPlayModeHandler::initializePhysicsAnimationsFor(const std::vector<EntityHandle>& candidates)
    {
        if (!physicsProvider)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();

        // Pre-load all skeleton data in parallel (I/O heavy)
        struct PhysAnimEntry
        {
            entt::entity entity;
            EntityHandle handle;
            std::string meshPath;
        };

        std::vector<PhysAnimEntry> entries;
        for (const auto& handle : candidates)
        {
            // createPhysicsAnimation is NOT idempotent — never re-init an entity that already has state
            // (avoids a duplicate ragdoll and skips redundant skeleton I/O on mid-Play re-fire).
            if (activePhysicsAnimationEntities.contains(handle))
                continue;

            entt::entity entity = internal::fromHandle(handle);
            if (!registry.valid(entity) ||
                !registry.all_of<components::PhysicsAnimationComponent,
                                 components::MeshComponent,
                                 components::TransformComponent>(entity))
                continue;

            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            if (!meshComp.meshRef.isValid() || !meshComp.animatorRef.isValid())
                continue;
            entries.push_back({entity, handle, meshComp.meshRef.resolve()});
        }

        if (entries.empty())
        {
            return;
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
        size_t createdCount = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto skeletonData = skeletonFutures[i].get();
            if (!skeletonData)
                continue;

            entt::entity entity = entries[i].entity;
            EntityHandle handle = entries[i].handle;
            const auto& transform = registry.get<components::TransformComponent>(entity);
            auto& physAnimComp = registry.get<components::PhysicsAnimationComponent>(entity);

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

            if (physAnimComp.config.defaultMode == types::PhysicsAnimationMode::Ragdoll ||
                physAnimComp.config.defaultMode == types::PhysicsAnimationMode::PoweredRagdoll)
            {
                physicsProvider->activateRagdoll(handle);
                physAnimComp.currentMode = physAnimComp.config.defaultMode;
            }
            else
            {
                physicsProvider->createKinematicBones(handle, transform.position);
                physAnimComp.currentMode = physAnimComp.config.defaultMode;
            }

            physAnimComp.isInitialized = true;
            activePhysicsAnimationEntities.insert(handle);
            ++createdCount;
        }

        if (createdCount > 0)
        {
            vfLogInfo("Initialized {} physics animation entities", createdCount);
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
