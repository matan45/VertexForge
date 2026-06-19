#include "RuntimeAnimatorSystem.hpp"
#include "AnimationLayerStack.hpp"
#include "IKPostProcess.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../services/events/animation/AnimationEventEvents.hpp"
#include "../../services/data/EntityConversion.hpp"
#include "asset/AssetRef.hpp"
#include <glm/gtc/quaternion.hpp>

namespace animation
{
    RuntimeAnimatorSystem& RuntimeAnimatorSystem::instance()
    {
        static RuntimeAnimatorSystem inst;
        return inst;
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

            // Apply pending animation state restore if available
            auto* uuidComp = registry.try_get<components::UUIDComponent>(pending.entity);
            if (uuidComp)
            {
                auto restoreIt = pendingRestores.find(uuidComp->id.getValue());
                if (restoreIt != pendingRestores.end())
                {
                    restoreSnapshot(pending.entity, restoreIt->second);
                    pendingRestores.erase(restoreIt);
                }
            }
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

        // VK-1408: for a root-motion-driven NavmeshAgent the detour crowd owns
        // translation; root motion only sets the pace. Publish the per-frame planar
        // distance (even when it is 0 on a loop wrap so the consumer's EMA sees it)
        // and skip the transform write so the two writers no longer stack.
        if (auto* navAgent = registry.try_get<components::NavmeshAgentComponent>(entity);
            navAgent && navAgent->rootMotionDriven)
        {
            auto& transform = registry.get<components::TransformComponent>(entity);
            const glm::vec3 scaled = delta * transform.scale;
            navAgent->rootMotionPlanarDistance = glm::length(glm::vec2(scaled.x, scaled.z));
            navAgent->rootMotionFresh = true;
            return;
        }

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

    void RuntimeAnimatorSystem::destroyEntityAnimator(entt::entity entity)
    {
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            animators.erase(it);
        }
        retargetContexts.erase(entity); // free after the layer stack (its evaluators) is gone
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
            retargetContexts.erase(entity);
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
        retargetContexts.clear();
        entityLODStates.clear();
        pendingRestores.clear();
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
        retargetContexts.clear();
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
