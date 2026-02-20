#include "RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/EditorLogger.hpp"
#include "../../services/events/SceneEvents.hpp"
#include "../../services/events/EditorModeEvents.hpp"
#include "../../services/events/AnimationEventEvents.hpp"
#include "../../services/events/SocketEvents.hpp"
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
                skeletonDataCache.clear();
                pendingCacheCleanup = true;

                // Flag all socket attachments for parent re-resolution (entity handles are now stale)
                auto& reg = scene::EntityRegistry::getRegistry();
                auto view = reg.view<components::SocketAttachmentComponent>();
                for (auto entity : view)
                {
                    reg.get<components::SocketAttachmentComponent>(entity).needsParentResolution = true;
                }

                vfLogInfo("[RuntimeAnimatorSystem] Cleared animators for mode change to {} - will reinitialize",
                          notification.currentMode == services::EditorMode::Play ? "Play" : "Edit");
            });

        socketDataSavedToken = dispatcher.subscribe<events::socket::SocketDataSavedNotification>(
            [this](const events::socket::SocketDataSavedNotification& notification)
            {
                skeletonDataCache.erase(notification.meshPath);
                // Also reset cached socket indices for attached entities
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::SocketAttachmentComponent>();
                for (auto entity : view)
                {
                    auto& attachment = registry.get<components::SocketAttachmentComponent>(entity);
                    attachment.cachedSocketIndex = -1;
                }
                vfLogInfo("[RuntimeAnimatorSystem] Skeleton cache invalidated for: {}", notification.meshPath);
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

        if (socketDataSavedToken.isValid())
        {
            dispatcher.unsubscribe(socketDataSavedToken);
            socketDataSavedToken = {};
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

        // Pass 1: Compute socket model-space transforms for entities with active animators
        socketTransformCache.clear();
        for (auto& [entity, animator] : animators)
        {
            if (!animator || !animator->isInitialized())
                continue;

            if (!registry.valid(entity))
                continue;

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
            auto& attachment = registry.get<components::SocketAttachmentComponent>(attachedEntity);

            // Resolve parent entity by name (only when flagged, not every frame)
            if (attachment.needsParentResolution && !attachment.parentEntityName.empty())
            {
                attachment.needsParentResolution = false;
                attachment.parentEntity = entt::null;
                attachment.cachedSocketIndex = -1;

                // First: search ancestors in hierarchy (handles prefab instances correctly)
                entt::entity ancestor = entt::null;
                if (registry.all_of<components::ParentComponent>(attachedEntity))
                {
                    ancestor = registry.get<components::ParentComponent>(attachedEntity).parent;
                }
                while (ancestor != entt::null && registry.valid(ancestor))
                {
                    if (registry.all_of<components::NameComponent>(ancestor) &&
                        registry.get<components::NameComponent>(ancestor).name == attachment.parentEntityName)
                    {
                        attachment.parentEntity = ancestor;
                        break;
                    }
                    if (registry.all_of<components::ParentComponent>(ancestor))
                        ancestor = registry.get<components::ParentComponent>(ancestor).parent;
                    else
                        break;
                }

                // Fallback: global search by name
                if (attachment.parentEntity == entt::null)
                {
                    auto nameView = registry.view<components::NameComponent>();
                    for (auto candidate : nameView)
                    {
                        if (nameView.get<components::NameComponent>(candidate).name == attachment.parentEntityName)
                        {
                            attachment.parentEntity = candidate;
                            break;
                        }
                    }
                }
            }

            if (!attachment.isActive || attachment.parentEntity == entt::null)
                continue;

            if (!registry.valid(attachment.parentEntity))
                continue;

            // Get skeleton for socket resolution
            const resource::SkeletonData* skeleton = nullptr;
            if (registry.all_of<components::MeshComponent>(attachment.parentEntity))
            {
                const auto& meshComp = registry.get<components::MeshComponent>(attachment.parentEntity);
                if (!meshComp.meshPath.empty())
                {
                    skeleton = loadSkeleton(meshComp.meshPath);
                }
            }

            // Resolve socket index if not cached
            int32_t socketIdx = attachment.cachedSocketIndex;
            if (socketIdx < 0 && skeleton)
            {
                socketIdx = skeleton->getSocketIndex(attachment.socketName);
                // If socket not found but we have a name, skeleton cache may be stale
                if (socketIdx < 0 && !attachment.socketName.empty())
                {
                    const auto& meshComp = registry.get<components::MeshComponent>(attachment.parentEntity);
                    skeletonDataCache.erase(meshComp.meshPath);
                    skeleton = loadSkeleton(meshComp.meshPath);
                    if (skeleton)
                    {
                        socketIdx = skeleton->getSocketIndex(attachment.socketName);
                    }
                }
                auto& mutableAttachment = registry.get<components::SocketAttachmentComponent>(attachedEntity);
                mutableAttachment.cachedSocketIndex = socketIdx;
            }

            if (socketIdx < 0)
                continue;

            // Try animated transforms first, fallback to bind-pose for edit mode
            glm::mat4 socketModelTransform = glm::mat4(1.0f);

            auto cacheIt = socketTransformCache.find(attachment.parentEntity);
            if (cacheIt != socketTransformCache.end() &&
                socketIdx < static_cast<int32_t>(cacheIt->second.size()))
            {
                // Animated socket transform
                socketModelTransform = cacheIt->second[socketIdx];
            }
            else if (skeleton && socketIdx < static_cast<int32_t>(skeleton->sockets.size()))
            {
                // Edit mode fallback: bone position + socket offset (matches preview)
                const auto& socket = skeleton->sockets[socketIdx];
                glm::vec3 boneMeshPos(0.0f);
                if (socket.boneIndex >= 0 &&
                    socket.boneIndex < static_cast<int32_t>(skeleton->bindPoses.size()))
                {
                    // Bone position in mesh space = globalInv * bindPose position
                    boneMeshPos = glm::vec3(
                        skeleton->globalInverseTransform
                        * skeleton->bindPoses[socket.boneIndex]
                        * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                }
                socketModelTransform = glm::translate(glm::mat4(1.0f),
                    boneMeshPos + socket.localPosition);
            }
            else
            {
                continue;
            }

            // Get parent world transform
            glm::mat4 parentWorld = glm::mat4(1.0f);
            if (registry.all_of<components::WorldTransformComponent>(attachment.parentEntity))
            {
                parentWorld = registry.get<components::WorldTransformComponent>(attachment.parentEntity).worldMatrix;
            }

            // Socket world = parent world transform * socket offset (in model space)
            // Entity's own rotation/scale are applied relative to the socket
            glm::mat4 socketWorld = parentWorld * socketModelTransform;

            // For socket-attached entities, only apply rotation and scale (not position)
            // Position is managed by the socket system and written back for display
            glm::mat4 entityLocal = glm::mat4(1.0f);
            if (registry.all_of<components::TransformComponent>(attachedEntity))
            {
                const auto& transform = registry.get<components::TransformComponent>(attachedEntity);
                glm::mat4 rot = glm::mat4_cast(glm::quat(glm::radians(transform.rotation)));
                entityLocal = rot * glm::scale(glm::mat4(1.0f), transform.scale);
            }

            glm::mat4 finalWorld = socketWorld * entityLocal;

            // Write to attached entity's WorldTransformComponent
            registry.get_or_emplace<components::WorldTransformComponent>(attachedEntity).worldMatrix = finalWorld;

            // Update TransformComponent position to reflect socket world position
            if (registry.all_of<components::TransformComponent>(attachedEntity))
            {
                auto& transform = registry.get<components::TransformComponent>(attachedEntity);
                transform.position = glm::vec3(finalWorld[3]);
                transform.isDirty = false;
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
