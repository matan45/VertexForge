#pragma once

#include "AnimatorStateMachine.hpp"
#include "animator/AnimatorTypes.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>
#include <string>

namespace animation
{
    class RuntimeAnimatorSystem
    {
    private:
        std::unordered_map<std::string, std::shared_ptr<animator::AnimatorData>> animatorDataCache;
        std::unordered_map<std::string, std::shared_ptr<resource::AnimationData>> animationDataCache;
        std::unordered_map<std::string, std::shared_ptr<resource::SkeletonData>> skeletonDataCache;

        std::unordered_map<entt::entity, std::unique_ptr<AnimatorStateMachine>> animators;

        events::SubscriptionToken meshDataChangedToken;
        events::SubscriptionToken editorModeChangedToken;
        events::SubscriptionToken socketDataSavedToken;

        std::unordered_map<entt::entity, std::vector<glm::mat4>> socketTransformCache;

        bool initialized = false;
        bool pendingCacheCleanup = false;

    public:
        static RuntimeAnimatorSystem& instance();

        void initialize();
        void shutdown();

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

        const std::vector<glm::mat4>* getCachedSocketTransforms(entt::entity entity) const;

    private:
        using ActiveAnimatorList = std::vector<std::pair<entt::entity, AnimatorStateMachine*>>;

        RuntimeAnimatorSystem() = default;
        ~RuntimeAnimatorSystem() = default;
        RuntimeAnimatorSystem(const RuntimeAnimatorSystem&) = delete;
        RuntimeAnimatorSystem& operator=(const RuntimeAnimatorSystem&) = delete;

        ActiveAnimatorList evaluateAnimations(float deltaTime);
        void publishAnimationEvents(entt::entity entity, AnimatorStateMachine* anim);
        void applyRootMotion(entt::entity entity, AnimatorStateMachine* anim, entt::registry& registry);
        void applyIKPostProcess(entt::entity entity, AnimatorStateMachine* anim, entt::registry& registry);

        const resource::AnimationData* loadAnimation(const std::string& path);

        void buildSocketTransformCache();
        void resolveAttachmentParent(entt::entity attachedEntity);
        void applyAttachmentTransform(entt::entity attachedEntity);
    };
}
