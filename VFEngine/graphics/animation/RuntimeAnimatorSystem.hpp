#pragma once

#include "AnimatorStateMachine.hpp"
#include "AnimationLOD.hpp"
#include "AnimationDataCache.hpp"
#include "SocketAttachmentUpdater.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "math/Frustum.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>

namespace animation
{
    struct AnimationCullingContext
    {
        math::Frustum frustum;
        glm::vec3 cameraPos{0.0f};
        bool enabled = false;
    };

    class RuntimeAnimatorSystem
    {
    private:
        AnimationDataCache dataCache;

        std::unordered_map<entt::entity, std::unique_ptr<AnimatorStateMachine>> animators;

        events::SubscriptionToken meshDataChangedToken;
        events::SubscriptionToken editorModeChangedToken;
        events::SubscriptionToken socketDataSavedToken;

        std::unique_ptr<SocketAttachmentUpdater> socketUpdater;

        AnimationCullingContext cullingContext;
        AnimationLODManager lodManager;
        std::unordered_map<entt::entity, EntityAnimationLODState> entityLODStates;
        uint32_t culledEntityCount = 0;

        struct PendingAnimatorInit
        {
            entt::entity entity;
            std::string animatorPath;
        };
        std::vector<PendingAnimatorInit> pendingInitQueue;
        mutable std::mutex pendingInitMutex;
        uint32_t maxInitPerFrame = 4;

        events::SubscriptionToken sectorLoadedToken;
        events::SubscriptionToken sectorUnloadedToken;

        bool initialized = false;
        bool pendingCacheCleanup = false;

        struct AnimationInstanceGroup
        {
            entt::entity leader = entt::null;
            std::vector<entt::entity> followers;
        };

        std::unordered_map<uint64_t, AnimationInstanceGroup> instanceGroups;
        uint32_t activeInstanceGroupCount = 0;

    public:
        static RuntimeAnimatorSystem& instance();

        void initialize();
        void shutdown();

        void setCullingContext(const math::Frustum& frustum, const glm::vec3& cameraPos);
        void clearCullingContext();
        uint32_t getCulledEntityCount() const { return culledEntityCount; }
        const AnimationCullingContext& getCullingContext() const { return cullingContext; }

        void updateAll(float deltaTime);
        void updateSocketAttachments();

        void initializeEntityAnimator(entt::entity entity, const std::string& animatorPath);
        void destroyEntityAnimator(entt::entity entity);
        bool hasAnimator(entt::entity entity) const;

        AnimatorStateMachine* getAnimator(entt::entity entity);
        const AnimatorStateMachine* getAnimator(entt::entity entity) const;

        void syncWithRegistry();

        void clearAll();
        void clearAnimatorInstances();
        void cleanupUnusedCaches();

        const resource::SkeletonData* loadSkeleton(const std::string& meshPath);

        uint32_t getTotalAnimatorCount() const { return static_cast<uint32_t>(animators.size()); }
        AnimationLODManager& getLODManager() { return lodManager; }
        const AnimationLODManager& getLODManager() const { return lodManager; }

        uint32_t getActiveInstanceGroupCount() const { return activeInstanceGroupCount; }

        void setMaxStreamingInitPerFrame(uint32_t count) { maxInitPerFrame = count; }
        uint32_t getMaxStreamingInitPerFrame() const { return maxInitPerFrame; }
        uint32_t getPendingInitCount() const { std::lock_guard<std::mutex> lock(pendingInitMutex); return static_cast<uint32_t>(pendingInitQueue.size()); }

        const std::vector<glm::mat4>* getCachedSocketTransforms(entt::entity entity) const;

    private:
        using ActiveAnimatorList = std::vector<std::pair<entt::entity, AnimatorStateMachine*>>;

        RuntimeAnimatorSystem() = default;
        ~RuntimeAnimatorSystem() = default;
        RuntimeAnimatorSystem(const RuntimeAnimatorSystem&) = delete;
        RuntimeAnimatorSystem& operator=(const RuntimeAnimatorSystem&) = delete;

        ActiveAnimatorList evaluateAnimations(float deltaTime);
        void processPendingStreamingInits();
        bool isEntityInFrustum(entt::entity entity, entt::registry& registry) const;
        void publishAnimationEvents(entt::entity entity, AnimatorStateMachine* anim);
        void applyRootMotion(entt::entity entity, AnimatorStateMachine* anim, entt::registry& registry);
        void applyIKPostProcess(entt::entity entity, AnimatorStateMachine* anim, entt::registry& registry);

        uint64_t computeInstanceGroupKey(const std::string& animatorPath, uint32_t stateId,
                                          uint8_t lodLevel, float normalizedTime) const;
    };
}
