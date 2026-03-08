#include "RuntimeAnimatorSystem.hpp"
#include "IKPostProcess.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "threading/JobSystem.hpp"
#include "../../services/events/project/SceneEvents.hpp"
#include "../../services/events/editor/EditorModeEvents.hpp"
#include "../../services/events/animation/AnimationEventEvents.hpp"
#include "../../services/events/physics/SocketEvents.hpp"
#include "../../services/events/world/WorldSectorEvents.hpp"
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

                auto& reg = scene::EntityRegistry::getRegistry();
                auto view = reg.view<components::SocketAttachmentComponent>();
                for (auto entity : view)
                {
                    reg.get<components::SocketAttachmentComponent>(entity).needsParentResolution = true;
                }
            });

        socketDataSavedToken = dispatcher.subscribe<events::socket::SocketDataSavedNotification>(
            [this](const events::socket::SocketDataSavedNotification& notification)
            {
                skeletonDataCache.erase(notification.meshPath);
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::SocketAttachmentComponent>();
                for (auto entity : view)
                {
                    auto& attachment = registry.get<components::SocketAttachmentComponent>(entity);
                    attachment.cachedSocketIndex = -1;
                }
            });

        // Streaming integration: queue animator init on sector load, cleanup on unload
        sectorLoadedToken = dispatcher.subscribe<events::world::SectorLoadedNotification>(
            [this](const events::world::SectorLoadedNotification&)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::MeshComponent>();
                std::lock_guard<std::mutex> lock(pendingInitMutex);
                for (auto entity : view)
                {
                    const auto& meshComp = view.get<components::MeshComponent>(entity);
                    if (!meshComp.animatorPath.empty() && !hasAnimator(entity))
                    {
                        pendingInitQueue.push_back({entity, meshComp.animatorPath});
                    }
                }
            });

        sectorUnloadedToken = dispatcher.subscribe<events::world::SectorUnloadedNotification>(
            [this](const events::world::SectorUnloadedNotification&)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                std::vector<entt::entity> toRemove;
                for (const auto& [entity, animator] : animators)
                {
                    if (!registry.valid(entity))
                    {
                        toRemove.push_back(entity);
                    }
                }
                for (auto entity : toRemove)
                {
                    destroyEntityAnimator(entity);
                }

                // Remove invalid entities from pending queue
                std::lock_guard<std::mutex> lock(pendingInitMutex);
                pendingInitQueue.erase(
                    std::remove_if(pendingInitQueue.begin(), pendingInitQueue.end(),
                        [&registry](const PendingAnimatorInit& pending) {
                            return !registry.valid(pending.entity);
                        }),
                    pendingInitQueue.end());
            });

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

        if (sectorLoadedToken.isValid())
        {
            dispatcher.unsubscribe(sectorLoadedToken);
            sectorLoadedToken = {};
        }

        if (sectorUnloadedToken.isValid())
        {
            dispatcher.unsubscribe(sectorUnloadedToken);
            sectorUnloadedToken = {};
        }

        {
            std::lock_guard<std::mutex> lock(pendingInitMutex);
            pendingInitQueue.clear();
        }
        clearAll();
        initialized = false;
    }

    void RuntimeAnimatorSystem::setCullingContext(const math::Frustum& frustum, const glm::vec3& cameraPos)
    {
        cullingContext.frustum = frustum;
        cullingContext.cameraPos = cameraPos;
        cullingContext.enabled = true;
    }

    void RuntimeAnimatorSystem::clearCullingContext()
    {
        cullingContext.enabled = false;
    }

    void RuntimeAnimatorSystem::processPendingStreamingInits()
    {
        std::vector<PendingAnimatorInit> batch;
        {
            std::lock_guard<std::mutex> lock(pendingInitMutex);
            if (pendingInitQueue.empty())
                return;

            // Take up to maxInitPerFrame items from the back
            uint32_t count = std::min(maxInitPerFrame, static_cast<uint32_t>(pendingInitQueue.size()));
            batch.assign(pendingInitQueue.end() - count, pendingInitQueue.end());
            pendingInitQueue.erase(pendingInitQueue.end() - count, pendingInitQueue.end());
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        for (auto& pending : batch)
        {
            if (!registry.valid(pending.entity))
                continue;

            if (hasAnimator(pending.entity))
                continue;

            initializeEntityAnimator(pending.entity, pending.animatorPath);
        }
    }

    void RuntimeAnimatorSystem::updateAll(float deltaTime)
    {
        processPendingStreamingInits();

        auto activeAnimators = evaluateAnimations(deltaTime);

        auto& registry = scene::EntityRegistry::getRegistry();
        for (auto& [entity, anim] : activeAnimators)
        {
            publishAnimationEvents(entity, anim);
            applyRootMotion(entity, anim, registry);
            applyIKPostProcess(entity, anim, registry);
        }
    }

    bool RuntimeAnimatorSystem::isEntityInFrustum(entt::entity entity, entt::registry& registry) const
    {
        if (!cullingContext.enabled || !cullingContext.frustum.isInitialized())
            return true;

        if (!registry.all_of<components::WorldTransformComponent>(entity))
            return true;

        const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
        glm::vec3 worldPos = glm::vec3(worldTransform.worldMatrix[3]);

        // Use a conservative bounding sphere for animated entities
        // Default radius of 3m covers most humanoid characters
        // 5% expansion margin to prevent rapid toggling at frustum edges
        constexpr float ANIM_ENTITY_RADIUS = 3.0f;
        constexpr float FRUSTUM_MARGIN = 1.05f;
        float radius = ANIM_ENTITY_RADIUS * FRUSTUM_MARGIN;

        // Extract scale from world matrix for better radius estimation
        float scaleX = glm::length(glm::vec3(worldTransform.worldMatrix[0]));
        float scaleY = glm::length(glm::vec3(worldTransform.worldMatrix[1]));
        float scaleZ = glm::length(glm::vec3(worldTransform.worldMatrix[2]));
        float maxScale = glm::max(scaleX, glm::max(scaleY, scaleZ));
        radius *= maxScale;

        // Sphere-frustum test: check against all 6 planes
        math::AABB aabb(worldPos - glm::vec3(radius), worldPos + glm::vec3(radius));
        return cullingContext.frustum.intersectsAABB(aabb);
    }

    RuntimeAnimatorSystem::ActiveAnimatorList RuntimeAnimatorSystem::evaluateAnimations(float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        culledEntityCount = 0;
        lodManager.resetLODCounts();
        instanceGroups.clear();
        activeInstanceGroupCount = 0;

        ActiveAnimatorList activeAnimators;
        std::vector<std::pair<entt::entity, AnimatorStateMachine*>> lodInterpolateEntities;

        // Collect candidates that pass frustum/LOD filtering
        struct EvalCandidate
        {
            entt::entity entity;
            AnimatorStateMachine* anim;
            uint8_t lodLevel;
        };
        std::vector<EvalCandidate> evalCandidates;

        for (auto& [entity, animator] : animators)
        {
            if (!animator || !animator->isInitialized() || !animator->isPlaying())
                continue;
            if (!registry.valid(entity))
                continue;

            // Frustum culling: skip bone evaluation for off-screen entities
            // Their last bone matrices remain in the GPU buffer
            if (!isEntityInFrustum(entity, registry))
            {
                ++culledEntityCount;
                continue;
            }

            // LOD: compute distance to camera and determine update frequency
            auto& lodState = entityLODStates[entity];
            if (cullingContext.enabled)
            {
                float distSq = 0.0f;
                if (registry.all_of<components::WorldTransformComponent>(entity))
                {
                    const auto& wt = registry.get<components::WorldTransformComponent>(entity);
                    glm::vec3 pos = glm::vec3(wt.worldMatrix[3]);
                    glm::vec3 diff = pos - cullingContext.cameraPos;
                    distSq = glm::dot(diff, diff);
                }
                lodManager.updateEntityLOD(lodState, distSq, deltaTime);
            }
            else
            {
                lodManager.updateEntityLOD(lodState, 0.0f, deltaTime);
            }

            lodManager.incrementLODCount(lodState.currentLOD);

            if (lodManager.shouldEvaluateThisFrame(lodState))
            {
                uint8_t lodLevel = static_cast<uint8_t>(lodState.currentLOD);
                evalCandidates.push_back({entity, animator.get(), lodLevel});
                lodState.framesSinceLastEval = 0;
            }
            else if (lodState.currentLOD == AnimationLODLevel::LOD1 &&
                     lodState.hasCachedPoseA && lodState.hasCachedPoseB)
            {
                // LOD 1 skipped frame: interpolate between cached poses
                lodInterpolateEntities.push_back({entity, animator.get()});
            }
            // LOD 3 (frozen): do nothing, last pose stays in GPU buffer
        }

        // Build instance groups from eval candidates
        // Entities that are blending have unique poses and cannot be grouped
        ActiveAnimatorList leadersToEvaluate;
        std::vector<std::pair<entt::entity, AnimatorStateMachine*>> followersToSync;

        for (auto& candidate : evalCandidates)
        {
            // Don't group blending entities - they have unique cross-state poses
            if (candidate.anim->isBlending())
            {
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
                continue;
            }

            const animator::AnimatorState* currentState = candidate.anim->getCurrentAnimatorState();
            if (!currentState)
            {
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
                continue;
            }

            // Get animator path from component
            std::string animatorPath;
            if (registry.all_of<components::AnimatorComponent>(candidate.entity))
            {
                animatorPath = registry.get<components::AnimatorComponent>(candidate.entity).animatorPath;
            }

            if (animatorPath.empty())
            {
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
                continue;
            }

            float normalizedTime = candidate.anim->getNormalizedStateTime();
            uint64_t groupKey = computeInstanceGroupKey(animatorPath, currentState->id,
                                                         candidate.lodLevel, normalizedTime);

            auto& group = instanceGroups[groupKey];
            if (group.leader == entt::null)
            {
                // First entity in this group becomes the leader
                group.leader = candidate.entity;
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
            }
            else
            {
                // Additional entities become followers
                group.followers.push_back(candidate.entity);
                followersToSync.push_back({candidate.entity, candidate.anim});
            }
        }

        // Count groups with 2+ members (actual instancing savings)
        for (const auto& [key, group] : instanceGroups)
        {
            if (!group.followers.empty())
            {
                ++activeInstanceGroupCount;
            }
        }

        // Evaluate leaders via job system (only leaders compute bone matrices)
        if (leadersToEvaluate.size() > 1)
        {
            std::vector<std::future<void>> futures;
            futures.reserve(leadersToEvaluate.size());

            for (auto& [entity, anim] : leadersToEvaluate)
            {
                futures.push_back(threading::JobSystem::instance().submit(
                    [anim, deltaTime]()
                    {
                        anim->update(deltaTime);
                    }, threading::JobPriority::HIGH
                ));
            }

            for (auto& f : futures)
            {
                f.get();
            }
        }
        else if (!leadersToEvaluate.empty())
        {
            leadersToEvaluate[0].second->update(deltaTime);
        }

        // Copy leader bone matrices to followers
        for (const auto& [key, group] : instanceGroups)
        {
            if (group.followers.empty())
                continue;

            auto leaderIt = animators.find(group.leader);
            if (leaderIt == animators.end())
                continue;

            const auto& leaderMatrices = leaderIt->second->getBoneMatrices();
            for (entt::entity follower : group.followers)
            {
                auto followerIt = animators.find(follower);
                if (followerIt != animators.end())
                {
                    followerIt->second->getMutableBoneMatrices() = leaderMatrices;
                }
            }
        }

        // Build final activeAnimators list: leaders + followers
        activeAnimators = std::move(leadersToEvaluate);
        for (auto& [entity, anim] : followersToSync)
        {
            activeAnimators.push_back({entity, anim});
        }

        // Cache poses for LOD 1 entities that were evaluated
        for (auto& [entity, anim] : activeAnimators)
        {
            auto it = entityLODStates.find(entity);
            if (it != entityLODStates.end() &&
                it->second.currentLOD == AnimationLODLevel::LOD1)
            {
                lodManager.cachePose(it->second, anim->getBoneMatrices());
            }
        }

        // Interpolate cached poses for LOD 1 entities on skipped frames
        for (auto& [entity, anim] : lodInterpolateEntities)
        {
            auto it = entityLODStates.find(entity);
            if (it != entityLODStates.end())
            {
                uint8_t lodIdx = static_cast<uint8_t>(it->second.currentLOD);
                uint32_t interval = lodManager.getConfig().updateIntervals[lodIdx];
                float t = interval > 0
                    ? static_cast<float>(it->second.framesSinceLastEval) / static_cast<float>(interval)
                    : 0.5f;
                std::vector<glm::mat4> interpolated;
                lodManager.interpolateCachedPoses(it->second, t, interpolated);
                if (!interpolated.empty())
                {
                    anim->getMutableBoneMatrices() = std::move(interpolated);
                    // Add to activeAnimators so bones get uploaded
                    activeAnimators.push_back({entity, anim});
                }
            }
        }

        return activeAnimators;
    }

    void RuntimeAnimatorSystem::publishAnimationEvents(entt::entity entity, AnimatorStateMachine* anim)
    {
        const auto& firedEvents = anim->getFiredEvents();
        if (firedEvents.empty())
            return;

        const animator::AnimatorState* currentState = anim->getCurrentAnimatorState();
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

    void RuntimeAnimatorSystem::applyRootMotion(entt::entity entity, AnimatorStateMachine* anim,
                                                 entt::registry& registry)
    {
        if (!registry.valid(entity) ||
            !registry.all_of<components::AnimatorComponent, components::TransformComponent>(entity))
            return;

        const auto& animComp = registry.get<components::AnimatorComponent>(entity);
        if (!animComp.applyRootMotion)
            return;

        glm::vec3 delta = anim->consumeRootMotionDelta();
        if (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f)
        {
            auto& transform = registry.get<components::TransformComponent>(entity);
            delta *= transform.scale;
            glm::quat rotation = glm::quat(glm::radians(transform.rotation));
            transform.position += rotation * delta;
            transform.isDirty = true;
        }
    }

    void RuntimeAnimatorSystem::applyIKPostProcess(entt::entity entity, AnimatorStateMachine* anim,
                                                    entt::registry& registry)
    {
        if (!registry.valid(entity) || !registry.all_of<components::IKTargetComponent>(entity))
            return;

        auto& ikComp = registry.get<components::IKTargetComponent>(entity);
        if (ikComp.chains.empty())
            return;

        const resource::SkeletonData* skeleton = nullptr;
        if (registry.all_of<components::MeshComponent>(entity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            if (!meshComp.meshPath.empty())
                skeleton = loadSkeleton(meshComp.meshPath);
        }

        if (skeleton)
        {
            auto& matrices = anim->getMutableBoneMatrices();
            IKPostProcessor::applyIK(matrices, *skeleton,
                                      ikComp.chains, ikComp.runtimeStates);
        }
    }

    void RuntimeAnimatorSystem::updateSocketAttachments()
    {
        buildSocketTransformCache();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto attachmentView = registry.view<components::SocketAttachmentComponent>();
        for (auto attachedEntity : attachmentView)
        {
            if (!registry.valid(attachedEntity))
                continue;
            resolveAttachmentParent(attachedEntity);
            applyAttachmentTransform(attachedEntity);
        }
    }

    void RuntimeAnimatorSystem::buildSocketTransformCache()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

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
    }

    void RuntimeAnimatorSystem::resolveAttachmentParent(entt::entity attachedEntity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(attachedEntity) || !registry.all_of<components::SocketAttachmentComponent>(attachedEntity))
        {
            return;
        }
        auto& attachment = registry.get<components::SocketAttachmentComponent>(attachedEntity);

        if (!attachment.needsParentResolution || attachment.parentEntityName.empty())
            return;

        attachment.needsParentResolution = false;
        attachment.parentEntity = entt::null;
        attachment.cachedSocketIndex = -1;

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

    void RuntimeAnimatorSystem::applyAttachmentTransform(entt::entity attachedEntity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(attachedEntity) || !registry.all_of<components::SocketAttachmentComponent>(attachedEntity))
            return;
        auto& attachment = registry.get<components::SocketAttachmentComponent>(attachedEntity);

        if (!attachment.isActive || attachment.parentEntity == entt::null)
            return;

        if (!registry.valid(attachment.parentEntity))
            return;

        const resource::SkeletonData* skeleton = nullptr;
        if (registry.all_of<components::MeshComponent>(attachment.parentEntity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(attachment.parentEntity);
            if (!meshComp.meshPath.empty())
            {
                skeleton = loadSkeleton(meshComp.meshPath);
            }
        }

        int32_t socketIdx = attachment.cachedSocketIndex;
        if (socketIdx < 0 && skeleton)
        {
            socketIdx = skeleton->getSocketIndex(attachment.socketName);
            if (socketIdx < 0 && !attachment.socketName.empty() &&
                registry.all_of<components::MeshComponent>(attachment.parentEntity))
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
            return;

        glm::mat4 socketModelTransform = glm::mat4(1.0f);

        auto cacheIt = socketTransformCache.find(attachment.parentEntity);
        if (cacheIt != socketTransformCache.end() &&
            socketIdx < static_cast<int32_t>(cacheIt->second.size()))
        {
            socketModelTransform = cacheIt->second[socketIdx];
        }
        else if (skeleton && socketIdx < static_cast<int32_t>(skeleton->sockets.size()))
        {
            const auto& socket = skeleton->sockets[socketIdx];
            glm::vec3 boneMeshPos(0.0f);
            if (socket.boneIndex >= 0 &&
                socket.boneIndex < static_cast<int32_t>(skeleton->bindPoses.size()))
            {
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
            return;
        }

        glm::mat4 parentWorld = glm::mat4(1.0f);
        if (registry.all_of<components::WorldTransformComponent>(attachment.parentEntity))
        {
            parentWorld = registry.get<components::WorldTransformComponent>(attachment.parentEntity).worldMatrix;
        }

        glm::mat4 socketWorld = parentWorld * socketModelTransform;

        glm::mat4 entityLocal = glm::mat4(1.0f);
        if (registry.all_of<components::TransformComponent>(attachedEntity))
        {
            const auto& transform = registry.get<components::TransformComponent>(attachedEntity);
            glm::mat4 rot = glm::mat4_cast(glm::quat(glm::radians(transform.rotation)));
            entityLocal = rot * glm::scale(glm::mat4(1.0f), transform.scale);
        }

        glm::mat4 finalWorld = socketWorld * entityLocal;

        registry.get_or_emplace<components::WorldTransformComponent>(attachedEntity).worldMatrix = finalWorld;

        if (registry.all_of<components::TransformComponent>(attachedEntity))
        {
            auto& transform = registry.get<components::TransformComponent>(attachedEntity);
            transform.position = glm::vec3(finalWorld[3]);
            transform.isDirty = false;
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
        entityLODStates.erase(entity);

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
        entityLODStates.clear();
        animatorDataCache.clear();
        animationDataCache.clear();
        skeletonDataCache.clear();
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
        entityLODStates.clear();
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
            vfLogInfo("Cleaned up {} animators, {} animations, {} skeletons",
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

    const std::vector<glm::mat4>* RuntimeAnimatorSystem::getCachedSocketTransforms(entt::entity entity) const
    {
        auto it = socketTransformCache.find(entity);
        if (it != socketTransformCache.end())
            return &it->second;
        return nullptr;
    }

    uint64_t RuntimeAnimatorSystem::computeInstanceGroupKey(const std::string& animatorPath, uint32_t stateId,
                                                             uint8_t lodLevel, float normalizedTime) const
    {
        // Quantize time to 0.05 intervals
        uint32_t quantizedTime = static_cast<uint32_t>(normalizedTime * 20.0f);

        uint64_t key = std::hash<std::string>{}(animatorPath);
        key = key * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(stateId);
        key = key * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(lodLevel);
        key = key * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(quantizedTime);
        return key;
    }
}
