#include "RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/EditorLogger.hpp"
#include "../../services/events/SceneEvents.hpp"
#include "../../services/events/EditorModeEvents.hpp"
#include "../../services/events/AnimationEventEvents.hpp"
#include "../../services/data/EntityConversion.hpp"
#include <glm/gtc/quaternion.hpp>
#include <unordered_set>

namespace animation
{
    RuntimeAnimatorSystem& RuntimeAnimatorSystem::instance()
    {
        static RuntimeAnimatorSystem inst;
        return inst;
    }

    void RuntimeAnimatorSystem::initialize()
    {
        if (initialized)
        {
            return;
        }
        initialized = true;

        auto& dispatcher = events::EventDispatcher::instance();
        meshDataChangedToken = dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                if (!notification.animatorPath.empty())
                {
                    return;
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity entity = services::internal::fromHandle(notification.entity);

                if (!registry.valid(entity))
                {
                    return;
                }

                if (hasAnimator(entity))
                {
                    destroyEntityAnimator(entity);
                }
            });

        editorModeChangedToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& notification)
            {
                clearAnimatorInstances();
                pendingCacheCleanup = true;
                vfLogInfo("[RuntimeAnimatorSystem] Cleared animators for mode change to {} - will reinitialize",
                          notification.currentMode == services::EditorMode::Play ? "Play" : "Edit");
            });

        vfLogInfo("[RuntimeAnimatorSystem] Initialized");
    }

    void RuntimeAnimatorSystem::shutdown()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (meshDataChangedToken.isValid())
        {
            dispatcher.unsubscribe(meshDataChangedToken);
            meshDataChangedToken = {};
        }

        if (editorModeChangedToken.isValid())
        {
            dispatcher.unsubscribe(editorModeChangedToken);
            editorModeChangedToken = {};
        }

        clearAll();
        initialized = false;
        vfLogInfo("[RuntimeAnimatorSystem] Shutdown");
    }

    void RuntimeAnimatorSystem::updateAll(float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto& [entity, animator] : animators)
        {
            if (!animator || !animator->isInitialized() || !animator->isPlaying())
                continue;

            animator->update(deltaTime);

            // Publish fired animation events
            const auto& firedEvents = animator->getFiredEvents();
            if (!firedEvents.empty())
            {
                const animator::AnimatorState* currentState = animator->getCurrentAnimatorState();
                std::string stateName = currentState ? currentState->name : "";
                auto entityHandle = services::internal::toHandle(entity);

                for (const auto* event : firedEvents)
                {
                    events::animation::AnimationEventFiredNotification notification;
                    notification.entity = entityHandle;
                    notification.eventName = event->name;
                    notification.stateName = stateName;
                    notification.payload = event->payload;
                    events::EventDispatcher::instance().publish(notification);
                }
            }

            // Apply root motion delta to entity transform
            if (registry.valid(entity) &&
                registry.all_of<components::AnimatorComponent, components::TransformComponent>(entity))
            {
                const auto& animComp = registry.get<components::AnimatorComponent>(entity);
                if (animComp.applyRootMotion)
                {
                    glm::vec3 delta = animator->consumeRootMotionDelta();
                    if (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f)
                    {
                        auto& transform = registry.get<components::TransformComponent>(entity);
                        // Delta is in model space; scale by entity scale to get world space
                        delta *= transform.scale;
                        glm::quat rotation = glm::quat(glm::radians(transform.rotation));
                        transform.position += rotation * delta;
                        transform.isDirty = true;
                    }
                }
            }
        }
    }

    void RuntimeAnimatorSystem::updateSocketAttachments()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Pass 1: Compute socket model-space transforms for all entities with animators and sockets
        socketTransformCache.clear();
        for (auto& [entity, animator] : animators)
        {
            if (!animator || !animator->isInitialized())
                continue;

            if (!registry.valid(entity))
                continue;

            // Get skeleton from cache to check for sockets
            const resource::SkeletonData* skeleton = nullptr;
            if (registry.all_of<components::MeshComponent>(entity))
            {
                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                if (!meshComp.meshPath.empty())
                {
                    skeleton = loadSkeleton(meshComp.meshPath);
                }
            }

            if (!skeleton || skeleton->sockets.empty())
                continue;

            auto& transforms = socketTransformCache[entity];
            animator->computeSocketTransforms(skeleton->sockets, transforms);
        }

        // Pass 2: Apply socket transforms to attached entities
        auto attachmentView = registry.view<components::SocketAttachmentComponent>();
        for (auto attachedEntity : attachmentView)
        {
            const auto& attachment = attachmentView.get<components::SocketAttachmentComponent>(attachedEntity);
            if (!attachment.isActive || attachment.parentEntity == entt::null)
                continue;

            if (!registry.valid(attachment.parentEntity))
                continue;

            // Find parent's cached socket transforms
            auto cacheIt = socketTransformCache.find(attachment.parentEntity);
            if (cacheIt == socketTransformCache.end() || cacheIt->second.empty())
                continue;

            // Resolve socket index if not cached
            int32_t socketIdx = attachment.cachedSocketIndex;
            if (socketIdx < 0)
            {
                // Look up by name
                const resource::SkeletonData* skeleton = nullptr;
                if (registry.all_of<components::MeshComponent>(attachment.parentEntity))
                {
                    const auto& meshComp = registry.get<components::MeshComponent>(attachment.parentEntity);
                    skeleton = loadSkeleton(meshComp.meshPath);
                }
                if (skeleton)
                {
                    socketIdx = skeleton->getSocketIndex(attachment.socketName);
                    // Cache it for future frames
                    auto& mutableAttachment = registry.get<components::SocketAttachmentComponent>(attachedEntity);
                    mutableAttachment.cachedSocketIndex = socketIdx;
                }
            }

            if (socketIdx < 0 || socketIdx >= static_cast<int32_t>(cacheIt->second.size()))
                continue;

            // Get parent world transform
            glm::mat4 parentWorld = glm::mat4(1.0f);
            if (registry.all_of<components::WorldTransformComponent>(attachment.parentEntity))
            {
                parentWorld = registry.get<components::WorldTransformComponent>(attachment.parentEntity).worldMatrix;
            }

            glm::mat4 socketWorld = parentWorld * cacheIt->second[socketIdx];

            // Write to attached entity's WorldTransformComponent
            registry.get_or_emplace<components::WorldTransformComponent>(attachedEntity).worldMatrix = socketWorld;

            // Clear dirty flag so SceneGraphSystem doesn't overwrite
            if (registry.all_of<components::TransformComponent>(attachedEntity))
            {
                registry.get<components::TransformComponent>(attachedEntity).isDirty = false;
            }
        }
    }

    void RuntimeAnimatorSystem::initializeEntityAnimator(entt::entity entity, const std::string& animatorPath)
    {
        if (animatorPath.empty())
        {
            return;
        }

        auto it = animators.find(entity);
        if (it != animators.end())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.all_of<components::AnimatorComponent>(entity))
            {
                auto& animComp = registry.get<components::AnimatorComponent>(entity);
                if (animComp.animatorPath == animatorPath && animComp.isInitialized)
                {
                    return;
                }
            }
            destroyEntityAnimator(entity);
        }

        auto cacheIt = animatorDataCache.find(animatorPath);
        std::shared_ptr<animator::AnimatorData> animatorData;

        if (cacheIt != animatorDataCache.end())
        {
            animatorData = cacheIt->second;
        }
        else
        {
            animatorData = resource::ResourceManager::loadAnimator(animatorPath);
            if (!animatorData)
            {
                vfLogError("[RuntimeAnimatorSystem] Failed to load animator: {}", animatorPath);
                return;
            }
            animatorDataCache[animatorPath] = animatorData;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        const resource::SkeletonData* skeleton = nullptr;
        std::string meshPath;

        if (registry.all_of<components::MeshComponent>(entity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            meshPath = meshComp.meshPath;
            if (!meshPath.empty())
            {
                skeleton = loadSkeleton(meshPath);
            }
        }

        if (!skeleton)
        {
            if (meshPath.empty())
            {
                vfLogWarning("[RuntimeAnimatorSystem] Entity {} has no mesh path - cannot create animator",
                             static_cast<uint32_t>(entity));
            }
            return;
        }

        auto stateMachine = std::make_unique<AnimatorStateMachine>();

        stateMachine->initialize(*animatorData, skeleton, [this](const std::string& path) -> const resource::AnimationData*
        {
            return loadAnimation(path);
        });

        AnimatorStateMachine* rawPtr = stateMachine.get();
        animators[entity] = std::move(stateMachine);

        if (!registry.all_of<components::AnimatorComponent>(entity))
        {
            registry.emplace<components::AnimatorComponent>(entity);
        }

        auto& animComp = registry.get<components::AnimatorComponent>(entity);
        animComp.stateMachine = rawPtr;
        animComp.animatorPath = animatorPath;
        animComp.isInitialized = true;

        // Sync root motion flag from MeshComponent (persistent storage)
        if (registry.all_of<components::MeshComponent>(entity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            animComp.applyRootMotion = meshComp.applyRootMotion;
        }
        rawPtr->setRootMotionEnabled(animComp.applyRootMotion);
    }

    void RuntimeAnimatorSystem::destroyEntityAnimator(entt::entity entity)
    {
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            animators.erase(it);
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.all_of<components::AnimatorComponent>(entity))
        {
            auto& animComp = registry.get<components::AnimatorComponent>(entity);
            animComp.stateMachine = nullptr;
            animComp.animatorPath.clear();
            animComp.isInitialized = false;
        }
    }

    bool RuntimeAnimatorSystem::hasAnimator(entt::entity entity) const
    {
        return animators.find(entity) != animators.end();
    }

    AnimatorStateMachine* RuntimeAnimatorSystem::getAnimator(entt::entity entity)
    {
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    const AnimatorStateMachine* RuntimeAnimatorSystem::getAnimator(entt::entity entity) const
    {
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    void RuntimeAnimatorSystem::syncWithRegistry()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        auto view = registry.view<components::MeshComponent>();
        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);

            if (!meshComp.animatorPath.empty())
            {
                if (!hasAnimator(entity))
                {
                    initializeEntityAnimator(entity, meshComp.animatorPath);
                }
            }
            else
            {
                if (hasAnimator(entity))
                {
                    destroyEntityAnimator(entity);
                }
            }
        }

        std::vector<entt::entity> toRemove;
        for (const auto& [entity, animator] : animators)
        {
            if (!registry.valid(entity) || !registry.all_of<components::MeshComponent>(entity))
            {
                toRemove.push_back(entity);
            }
        }

        for (auto entity : toRemove)
        {
            animators.erase(entity);
            vfLogInfo("[RuntimeAnimatorSystem] Cleaned up animator for removed entity {}",
                      static_cast<uint32_t>(entity));
        }

        if (pendingCacheCleanup)
        {
            pendingCacheCleanup = false;
            cleanupUnusedCaches();
        }
    }

    void RuntimeAnimatorSystem::clearAll()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        for (const auto& [entity, animator] : animators)
        {
            if (registry.valid(entity) && registry.all_of<components::AnimatorComponent>(entity))
            {
                auto& animComp = registry.get<components::AnimatorComponent>(entity);
                animComp.stateMachine = nullptr;
                animComp.isInitialized = false;
            }
        }

        animators.clear();
        animatorDataCache.clear();
        animationDataCache.clear();
        skeletonDataCache.clear();
        vfLogInfo("[RuntimeAnimatorSystem] Cleared all animators");
    }

    void RuntimeAnimatorSystem::clearAnimatorInstances()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        for (const auto& [entity, animator] : animators)
        {
            if (registry.valid(entity) && registry.all_of<components::AnimatorComponent>(entity))
            {
                auto& animComp = registry.get<components::AnimatorComponent>(entity);
                animComp.stateMachine = nullptr;
                animComp.isInitialized = false;
            }
        }

        animators.clear();
        vfLogInfo("[RuntimeAnimatorSystem] Cleared animator instances (caches preserved)");
    }

    void RuntimeAnimatorSystem::cleanupUnusedCaches()
    {
        std::unordered_set<std::string> usedAnimatorPaths;
        std::unordered_set<std::string> usedMeshPaths;
        std::unordered_set<std::string> usedAnimationPaths;

        auto& registry = scene::EntityRegistry::getRegistry();
        for (const auto& [entity, animator] : animators)
        {
            if (!animator)
            {
                continue;
            }

            if (registry.valid(entity) && registry.all_of<components::AnimatorComponent>(entity))
            {
                const auto& animComp = registry.get<components::AnimatorComponent>(entity);
                if (!animComp.animatorPath.empty())
                {
                    usedAnimatorPaths.insert(animComp.animatorPath);
                }
            }

            if (registry.valid(entity) && registry.all_of<components::MeshComponent>(entity))
            {
                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                if (!meshComp.meshPath.empty())
                {
                    usedMeshPaths.insert(meshComp.meshPath);
                }
            }

            const animator::AnimatorData* animData = animator->getAnimatorData();
            if (animData)
            {
                for (const auto& state : animData->graph.states)
                {
                    if (!state.animationPath.empty())
                    {
                        usedAnimationPaths.insert(state.animationPath);
                    }
                }
            }
        }

        size_t removedAnimators = 0;
        size_t removedAnimations = 0;
        size_t removedSkeletons = 0;

        for (auto it = animatorDataCache.begin(); it != animatorDataCache.end();)
        {
            if (usedAnimatorPaths.find(it->first) == usedAnimatorPaths.end())
            {
                it = animatorDataCache.erase(it);
                ++removedAnimators;
            }
            else
            {
                ++it;
            }
        }

        for (auto it = animationDataCache.begin(); it != animationDataCache.end();)
        {
            if (usedAnimationPaths.find(it->first) == usedAnimationPaths.end())
            {
                it = animationDataCache.erase(it);
                ++removedAnimations;
            }
            else
            {
                ++it;
            }
        }

        for (auto it = skeletonDataCache.begin(); it != skeletonDataCache.end();)
        {
            if (usedMeshPaths.find(it->first) == usedMeshPaths.end())
            {
                it = skeletonDataCache.erase(it);
                ++removedSkeletons;
            }
            else
            {
                ++it;
            }
        }

        if (removedAnimators > 0 || removedAnimations > 0 || removedSkeletons > 0)
        {
            vfLogInfo("[RuntimeAnimatorSystem] Cache cleanup: removed {} animators, {} animations, {} skeletons",
                      removedAnimators, removedAnimations, removedSkeletons);
        }
    }

    const resource::AnimationData* RuntimeAnimatorSystem::loadAnimation(const std::string& path)
    {
        if (path.empty())
        {
            return nullptr;
        }

        auto it = animationDataCache.find(path);
        if (it != animationDataCache.end())
        {
            return it->second.get();
        }

        auto future = resource::ResourceManager::loadAnimationAsync(path);
        auto animData = future.get();

        if (!animData)
        {
            vfLogError("[RuntimeAnimatorSystem] Failed to load animation: {}", path);
            return nullptr;
        }

        animationDataCache[path] = animData;

        return animData.get();
    }

    const resource::SkeletonData* RuntimeAnimatorSystem::loadSkeleton(const std::string& meshPath)
    {
        if (meshPath.empty())
        {
            return nullptr;
        }

        auto it = skeletonDataCache.find(meshPath);
        if (it != skeletonDataCache.end())
        {
            return it->second.get();
        }

        auto stream = resource::MeshStreamResource::openStream(meshPath);
        if (!stream)
        {
            vfLogError("[RuntimeAnimatorSystem] Failed to open mesh for skeleton: {}", meshPath);
            return nullptr;
        }

        if (!stream->hasSkeletonData())
        {
            skeletonDataCache[meshPath] = nullptr;
            vfLogWarning("[RuntimeAnimatorSystem] Mesh has no skeleton data: {}", meshPath);
            return nullptr;
        }

        auto skeletonData = std::make_shared<resource::SkeletonData>();
        if (!stream->readSkeleton(*skeletonData))
        {
            skeletonDataCache[meshPath] = nullptr;
            vfLogError("[RuntimeAnimatorSystem] Failed to read skeleton from: {}", meshPath);
            return nullptr;
        }

        skeletonDataCache[meshPath] = skeletonData;

        return skeletonData.get();
    }
}
