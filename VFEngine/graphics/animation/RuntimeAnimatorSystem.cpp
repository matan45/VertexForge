#include "RuntimeAnimatorSystem.hpp"
#include "AnimationLayerStack.hpp"
#include "IKPostProcess.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "asset/AssetRef.hpp"
#include "threading/JobSystem.hpp"
#include "../../services/events/project/SceneEvents.hpp"
#include "../../services/events/editor/EditorModeEvents.hpp"
#include "../../services/events/animation/AnimationEventEvents.hpp"
#include "../../services/events/physics/SocketEvents.hpp"
#include "../../services/events/world/WorldSectorEvents.hpp"
#include "../../services/data/EntityConversion.hpp"
#include <glm/gtc/quaternion.hpp>

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

        socketUpdater = std::make_unique<SocketAttachmentUpdater>(animators, dataCache);

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
                dataCache.clearSkeletons();
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
                dataCache.invalidateSkeleton(notification.meshPath);
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::SocketAttachmentComponent>();
                for (auto entity : view)
                {
                    auto& attachment = registry.get<components::SocketAttachmentComponent>(entity);
                    attachment.cachedSocketIndex = -1;
                }
            });

        sectorLoadedToken = dispatcher.subscribe<events::world::SectorLoadedNotification>(
            [this](const events::world::SectorLoadedNotification&)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::MeshComponent>();
                std::lock_guard<std::mutex> lock(pendingInitMutex);
                for (auto entity : view)
                {
                    const auto& meshComp = view.get<components::MeshComponent>(entity);
                    if (meshComp.animatorRef.isValid() && !hasAnimator(entity))
                    {
                        pendingInitQueue.push_back({entity, meshComp.animatorRef.resolve()});
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
        socketUpdater.reset();
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

        float scaleX = glm::length(glm::vec3(worldTransform.worldMatrix[0]));
        float scaleY = glm::length(glm::vec3(worldTransform.worldMatrix[1]));
        float scaleZ = glm::length(glm::vec3(worldTransform.worldMatrix[2]));
        float maxScale = glm::max(scaleX, glm::max(scaleY, scaleZ));
        radius *= maxScale;

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
        std::vector<std::pair<entt::entity, AnimationLayerStack*>> lodInterpolateEntities;

        struct EvalCandidate
        {
            entt::entity entity;
            AnimationLayerStack* anim;
            uint8_t lodLevel;
        };
        std::vector<EvalCandidate> evalCandidates;

        for (auto& [entity, animator] : animators)
        {
            if (!animator || !animator->isInitialized() || !animator->isPlaying())
                continue;
            if (!registry.valid(entity))
                continue;

            if (!isEntityInFrustum(entity, registry))
            {
                ++culledEntityCount;
                continue;
            }

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
                lodManager.updateEntityLOD(lodState, distSq);
            }
            else
            {
                lodManager.updateEntityLOD(lodState, 0.0f);
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
                lodInterpolateEntities.push_back({entity, animator.get()});
            }
        }

        ActiveAnimatorList leadersToEvaluate;
        std::vector<std::pair<entt::entity, AnimationLayerStack*>> followersToSync;

        for (auto& candidate : evalCandidates)
        {
            if (candidate.anim->isBlending())
            {
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
                continue;
            }

            const AnimatorStateMachine* baseSM = candidate.anim->getBaseStateMachine();
            const animator::AnimatorState* currentState = baseSM ? baseSM->getCurrentAnimatorState() : nullptr;
            if (!currentState)
            {
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
                continue;
            }

            std::string animatorPath;
            if (registry.all_of<components::AnimatorComponent>(candidate.entity))
            {
                animatorPath = registry.get<components::AnimatorComponent>(candidate.entity).animatorRef.resolve();
            }

            if (animatorPath.empty())
            {
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
                continue;
            }

            float normalizedTime = candidate.anim->getNormalizedTime();
            uint64_t groupKey = computeInstanceGroupKey(animatorPath, currentState->id,
                                                         candidate.lodLevel, normalizedTime);

            auto& group = instanceGroups[groupKey];
            if (group.leader == entt::null)
            {
                group.leader = candidate.entity;
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
            }
            else
            {
                group.followers.push_back(candidate.entity);
                followersToSync.push_back({candidate.entity, candidate.anim});
            }
        }

        for (const auto& [key, group] : instanceGroups)
        {
            if (!group.followers.empty())
            {
                ++activeInstanceGroupCount;
            }
        }

        if (leadersToEvaluate.size() > 1)
        {
            uint32_t leaderCount = static_cast<uint32_t>(leadersToEvaluate.size());
            threading::JobSystem::instance().parallelFor(leaderCount,
                [&leadersToEvaluate, deltaTime](uint32_t begin, uint32_t end)
                {
                    for (uint32_t i = begin; i < end; ++i)
                    {
                        leadersToEvaluate[i].second->update(deltaTime);
                    }
                }, 1); // minBatchSize=1: each animation update is expensive
        }
        else if (!leadersToEvaluate.empty())
        {
            leadersToEvaluate[0].second->update(deltaTime);
        }

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

        activeAnimators = std::move(leadersToEvaluate);
        for (auto& [entity, anim] : followersToSync)
        {
            activeAnimators.push_back({entity, anim});
        }

        for (auto& [entity, anim] : activeAnimators)
        {
            auto it = entityLODStates.find(entity);
            if (it != entityLODStates.end() &&
                it->second.currentLOD == AnimationLODLevel::LOD1)
            {
                lodManager.cachePose(it->second, anim->getBoneMatrices());
            }
        }

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
                    activeAnimators.push_back({entity, anim});
                }
            }
        }

        return activeAnimators;
    }

    void RuntimeAnimatorSystem::publishAnimationEvents(entt::entity entity, AnimationLayerStack* anim)
    {
        auto firedEvents = anim->getFiredEvents();
        if (firedEvents.empty())
            return;

        std::string stateName = anim->getCurrentStateName();
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

    void RuntimeAnimatorSystem::applyRootMotion(entt::entity entity, AnimationLayerStack* anim,
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

    void RuntimeAnimatorSystem::applyIKPostProcess(entt::entity entity, AnimationLayerStack* anim,
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
            if (meshComp.meshRef.isValid())
                skeleton = dataCache.loadSkeleton(meshComp.meshRef.resolve());
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
        socketUpdater->update();
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
                if (animComp.animatorRef.resolve() == animatorPath && animComp.isInitialized)
                {
                    return;
                }
            }
            destroyEntityAnimator(entity);
        }

        auto animatorData = dataCache.loadAnimatorData(animatorPath);
        if (!animatorData)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        const resource::SkeletonData* skeleton = nullptr;
        std::string meshPath;

        if (registry.all_of<components::MeshComponent>(entity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            meshPath = meshComp.meshRef.resolve();
            if (!meshPath.empty())
            {
                skeleton = dataCache.loadSkeleton(meshPath);
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

        auto layerStack = std::make_unique<AnimationLayerStack>();

        layerStack->initialize(*animatorData, skeleton, [this](const std::string& path) -> const resource::AnimationData*
        {
            return dataCache.loadAnimation(path);
        });

        AnimationLayerStack* rawPtr = layerStack.get();
        animators[entity] = std::move(layerStack);

        if (!registry.all_of<components::AnimatorComponent>(entity))
        {
            registry.emplace<components::AnimatorComponent>(entity);
        }

        auto& animComp = registry.get<components::AnimatorComponent>(entity);
        animComp.stateMachine = rawPtr->getBaseStateMachine();
        animComp.animatorRef = asset::AssetRef::fromPath(animatorPath);
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
            animComp.animatorRef = asset::AssetRef::invalid();
            animComp.isInitialized = false;
        }
    }

    bool RuntimeAnimatorSystem::hasAnimator(entt::entity entity) const
    {
        return animators.find(entity) != animators.end();
    }

    AnimationLayerStack* RuntimeAnimatorSystem::getLayerStack(entt::entity entity)
    {
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    const AnimationLayerStack* RuntimeAnimatorSystem::getLayerStack(entt::entity entity) const
    {
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    AnimatorStateMachine* RuntimeAnimatorSystem::getAnimator(entt::entity entity)
    {
        auto* stack = getLayerStack(entity);
        return stack ? stack->getBaseStateMachine() : nullptr;
    }

    const AnimatorStateMachine* RuntimeAnimatorSystem::getAnimator(entt::entity entity) const
    {
        auto* stack = getLayerStack(entity);
        return stack ? stack->getBaseStateMachine() : nullptr;
    }

    void RuntimeAnimatorSystem::syncWithRegistry()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        auto view = registry.view<components::MeshComponent>();
        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);

            if (meshComp.animatorRef.isValid())
            {
                if (!hasAnimator(entity))
                {
                    initializeEntityAnimator(entity, meshComp.animatorRef.resolve());
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
        dataCache.clearAll();
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
        dataCache.cleanupUnused(animators);
    }

    const resource::SkeletonData* RuntimeAnimatorSystem::loadSkeleton(const std::string& meshPath)
    {
        return dataCache.loadSkeleton(meshPath);
    }

    const std::vector<glm::mat4>* RuntimeAnimatorSystem::getCachedSocketTransforms(entt::entity entity) const
    {
        return socketUpdater->getCachedSocketTransforms(entity);
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
