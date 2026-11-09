#include "RuntimeAnimatorSystem.hpp"
#include "AnimationLayerStack.hpp"
#include "IKPostProcess.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../services/events/animation/AnimationEventEvents.hpp"
#include "../../services/data/EntityConversion.hpp"
#include "asset/AssetRef.hpp"
#include "resource/ResourceManager.hpp"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <utility>

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
        {
            std::lock_guard<std::mutex> lock(pendingRequestMutex);
            pendingRequests = PendingRequests{};
            hasPendingRequests = false;
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

        // The detour crowd owns a NavmeshAgent's translation, so root motion must never be
        // added to the transform for one (that would double-move on top of the crowd).
        //   - rootMotionDriven (VK-1408): publish the per-frame planar distance (even 0 on a
        //     loop wrap so the consumer's EMA sees it) to PACE the crowd's maxSpeed.
        //   - otherwise: root motion is purely cosmetic. It was extracted only to keep the
        //     body rendering in-place (AnimationEvaluator pins the root bone to its bind pose)
        //     while the crowd moves the entity at its configured maxSpeed (nav-paced
        //     locomotion). If no crowd driver has taken ownership yet, apply the delta
        //     normally below so root-motion-only agents can still move.
        if (auto* navAgent = registry.try_get<components::NavmeshAgentComponent>(entity))
        {
            if (navAgent->rootMotionDriven)
            {
                auto& transform = registry.get<components::TransformComponent>(entity);
                const glm::vec3 scaled = delta * transform.scale;
                navAgent->rootMotionPlanarDistance =
                    glm::length(glm::vec2(scaled.x, scaled.z)) * navAgent->rootMotionSpeedScale;
                navAgent->rootMotionFresh = true;
                return;
            }
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
            // IK targets are world-space; the solver runs in model space. Pass the
            // entity's world matrix so applyIK can convert (identity if absent).
            glm::mat4 entityWorld(1.0f);
            if (registry.all_of<components::WorldTransformComponent>(entity))
                entityWorld = registry.get<components::WorldTransformComponent>(entity).worldMatrix;

            auto& matrices = anim->getMutableBoneMatrices();
            IKPostProcessor::applyIK(matrices, *skeleton,
                                      ikComp.chains, ikComp.runtimeStates, entityWorld);
        }
    }

    void RuntimeAnimatorSystem::updateSocketAttachments()
    {
        socketUpdater->update();
    }

    void RuntimeAnimatorSystem::destroyEntityAnimator(entt::entity entity)
    {
        editPreviewDirty = true;
        editPreviews.erase(entity); // a preview session never outlives the entity's animator
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            animators.erase(it);
        }
        retargetContexts.erase(entity); // free after the layer stack (its evaluators) is gone
        animatorSources.erase(entity);
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
        applyPendingRequests();

        auto& registry = scene::EntityRegistry::getRegistry();

        // AnimatorComponent only mirrors a live binding (setupEntityAnimatorComponent creates
        // it), so it goes with the binding. Removed after the view loop.
        std::vector<entt::entity> unbound;

        auto view = registry.view<components::MeshComponent>();
        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);

            if (meshComp.animatorRef.isValid())
            {
                syncEntityAnimator(entity, meshComp);
            }
            else
            {
                if (hasAnimator(entity))
                {
                    destroyEntityAnimator(entity);
                }
                if (registry.all_of<components::AnimatorComponent>(entity))
                {
                    unbound.push_back(entity);
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
            editPreviews.erase(entity);
            animators.erase(entity);
            retargetContexts.erase(entity);
            animatorSources.erase(entity);
            if (registry.valid(entity))
            {
                unbound.push_back(entity);
            }
        }

        for (auto entity : unbound)
        {
            registry.remove<components::AnimatorComponent>(entity);
        }
    }

    void RuntimeAnimatorSystem::syncEntityAnimator(entt::entity entity, const components::MeshComponent& meshComp)
    {
        if (hasAnimator(entity) && !animatorSourcesMatch(entity, meshComp))
        {
            destroyEntityAnimator(entity);
        }
        if (!hasAnimator(entity))
        {
            initializeEntityAnimator(entity, meshComp.animatorRef.resolve());
        }
    }

    void RuntimeAnimatorSystem::recordAnimatorSources(entt::entity entity, const std::string& animatorPath)
    {
        AnimatorSources& sources = animatorSources[entity];
        sources = AnimatorSources{};

        auto& registry = scene::EntityRegistry::getRegistry();
        const auto* meshComp = registry.try_get<components::MeshComponent>(entity);
        if (!meshComp)
            return;

        // A path that is no longer the component's (a stale queued init) records no animator
        // GUID, so the next sync rebuilds the animator from the component's current ref.
        if (meshComp->animatorRef.resolve() == animatorPath)
            sources.animator = meshComp->animatorRef.getGUID();
        sources.mesh = meshComp->meshRef.getGUID();
        sources.retarget = meshComp->retargetRef.getGUID();
    }

    bool RuntimeAnimatorSystem::animatorSourcesMatch(entt::entity entity, const components::MeshComponent& meshComp) const
    {
        auto it = animatorSources.find(entity);
        return it != animatorSources.end() &&
               it->second.animator == meshComp.animatorRef.getGUID() &&
               it->second.mesh == meshComp.meshRef.getGUID() &&
               it->second.retarget == meshComp.retargetRef.getGUID();
    }

    void RuntimeAnimatorSystem::requestAnimatorReload(std::string path)
    {
        {
            std::lock_guard<std::mutex> lock(pendingRequestMutex);
            pendingRequests.animatorPaths.push_back(std::move(path));
            hasPendingRequests = true;
        }
        editPreviewDirty = true;
    }

    void RuntimeAnimatorSystem::requestMeshReload(std::string path)
    {
        {
            std::lock_guard<std::mutex> lock(pendingRequestMutex);
            pendingRequests.meshPaths.push_back(std::move(path));
            hasPendingRequests = true;
        }
        editPreviewDirty = true;
    }

    void RuntimeAnimatorSystem::requestModeTransition()
    {
        {
            std::lock_guard<std::mutex> lock(pendingRequestMutex);
            pendingRequests.modeTransition = true;
            hasPendingRequests = true;
        }
        editPreviewDirty = true;
    }

    void RuntimeAnimatorSystem::markEditPreviewDirty()
    {
        editPreviewDirty = true;
    }

    void RuntimeAnimatorSystem::applyPendingRequests()
    {
        if (!hasPendingRequests.exchange(false))
            return;

        PendingRequests requests;
        {
            std::lock_guard<std::mutex> lock(pendingRequestMutex);
            std::swap(requests, pendingRequests);
        }

        // A mode change only ends the previews. Animators stay: scripts may already have set
        // parameters this frame, and Stop re-creates the entities, which the sync picks up.
        if (requests.modeTransition)
        {
            stopAllEditPreviews();
            editPreviewDirty = true;
        }

        for (const auto& path : requests.animatorPaths)
        {
            reloadAnimator(path);
        }

        if (!requests.meshPaths.empty())
        {
            reloadMeshes(requests.meshPaths);
        }
    }

    void RuntimeAnimatorSystem::reloadAnimator(const std::string& path)
    {
        const asset::AssetRef ref = asset::AssetRef::fromPath(path);
        if (!ref.isValid())
            return;

        // Layer stacks hold raw pointers into the cached AnimatorData. `stale` keeps the dropped
        // data alive until every stack built from the file is destroyed: those recorded with its
        // GUID, and any whose data is a dropped entry (a path spelled differently at init).
        const auto stale = dataCache.invalidateAnimatorData(ref.getGUID());

        std::vector<entt::entity> affected;
        for (const auto& [entity, stack] : animators)
        {
            const auto sourcesIt = animatorSources.find(entity);
            const bool fromFile = sourcesIt != animatorSources.end() && sourcesIt->second.animator == ref.getGUID();
            const animator::AnimatorData* data = stack ? stack->getAnimatorData() : nullptr;
            const bool fromStaleData = data && std::any_of(stale.begin(), stale.end(),
                [data](const std::shared_ptr<animator::AnimatorData>& dropped) { return dropped.get() == data; });
            if (fromFile || fromStaleData)
            {
                affected.push_back(entity);
            }
        }

        for (auto entity : affected)
        {
            destroyEntityAnimator(entity); // also ends the entity's edit preview
        }

        resource::ResourceManager::invalidateAnimatorCache(ref);
    }

    void RuntimeAnimatorSystem::reloadMeshes(const std::vector<std::string>& meshPaths)
    {
        // Layer stacks and preview sessions point at cached skeletons, and retarget contexts are
        // built from them (incl. another mesh's skeleton as the retarget source), so every
        // animator goes before any skeleton entry. The sync that follows re-reads the meshes.
        stopAllEditPreviews();
        std::vector<entt::entity> all;
        all.reserve(animators.size());
        for (const auto& [entity, stack] : animators)
        {
            all.push_back(entity);
        }
        for (auto entity : all)
        {
            destroyEntityAnimator(entity);
        }

        for (const auto& path : meshPaths)
        {
            dataCache.invalidateSkeleton(path);
            // Cache keys are MeshComponent::meshRef.resolve() spellings; the publisher's may differ.
            const std::string canonicalPath = asset::AssetRef::fromPath(path).resolve();
            if (!canonicalPath.empty() && canonicalPath != path)
            {
                dataCache.invalidateSkeleton(canonicalPath);
            }
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::SocketAttachmentComponent>();
        for (auto entity : view)
        {
            auto& attachment = view.get<components::SocketAttachmentComponent>(entity);
            attachment.cachedSocketIndex = -1;
            attachment.parentKind = components::SocketAttachmentComponent::ParentKind::Unknown;
        }

        if (socketUpdater)
        {
            socketUpdater->resetMissingSocketWarnings();
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

        // Preview sessions and layer stacks point into the data cache: both go first.
        editPreviews.clear();
        animators.clear();
        retargetContexts.clear();
        animatorSources.clear();
        entityLODStates.clear();
        pendingRestores.clear();
        dataCache.clearAll();
    }

    const resource::SkeletonData* RuntimeAnimatorSystem::loadSkeleton(const std::string& meshPath)
    {
        return dataCache.loadSkeleton(meshPath);
    }

    const std::vector<animator::SocketDefinition>* RuntimeAnimatorSystem::loadSockets(const std::string& meshPath)
    {
        return dataCache.loadSockets(meshPath);
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
