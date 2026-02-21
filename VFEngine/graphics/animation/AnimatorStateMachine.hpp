#pragma once

#include "animator/AnimatorTypes.hpp"
#include "animator/AnimationEventTypes.hpp"
#include "animator/SocketTypes.hpp"
#include "AnimationBlender.hpp"
#include "AnimationEvaluator.hpp"
#include "resource/Types.hpp"
#include <glm/glm.hpp>
#include <functional>

namespace animation
{
    using AnimationLoadCallback = std::function<const resource::AnimationData*(const std::string& path)>;

    struct AnimatorStateMachineState
    {
        uint32_t currentStateId = 0;
        uint32_t previousStateId = 0;
        float stateTime = 0.0f;
        float previousStateTime = 0.0f;
        float blendWeight = 0.0f;
        float blendDuration = 0.0f;
        float blendElapsed = 0.0f;
        bool isBlending = false;
        bool isPlaying = true;

        uint32_t currentLoopCount = 0;
        uint32_t exitTimeEvaluatedAtLoop = 0;
        float previousNormalizedTime = 0.0f;
    };

    class AnimatorStateMachine
    {
    private:
        const animator::AnimatorData* animatorData = nullptr;
        const resource::SkeletonData* skeletonData = nullptr;
        AnimationLoadCallback animationLoadCallback;
        animator::AnimatorRuntimeParameters parameters;
        AnimatorStateMachineState state;

        AnimationEvaluator currentEvaluator;
        AnimationEvaluator previousEvaluator;

        std::unordered_map<uint32_t, const resource::AnimationData*> loadedAnimations;

        std::vector<glm::mat4> currentBoneMatrices;

        bool initialized = false;

        bool rootMotionEnabled = false;
        bool rootMotionFirstFrame = true;
        glm::vec3 previousRootPosition{0.0f};
        glm::vec3 rootMotionDelta{0.0f};
        uint32_t rootMotionLastLoopCount = 0;

        uint32_t eventLastLoopCount = 0;
    public:
       explicit AnimatorStateMachine();
        ~AnimatorStateMachine();

        void initialize(const animator::AnimatorData& data, const resource::SkeletonData* skeleton, AnimationLoadCallback loadCallback);

        void update(float deltaTime);

        const std::vector<glm::mat4>& getBoneMatrices() const { return currentBoneMatrices; }

        void computeSocketTransforms(const std::vector<animator::SocketDefinition>& sockets,
                                     std::vector<glm::mat4>& outSocketModelTransforms) const;

        void setFloat(const std::string& name, float value);
        void setInt(const std::string& name, int32_t value);
        void setBool(const std::string& name, bool value);
        void setTrigger(const std::string& name);

        float getFloat(const std::string& name) const;
        int32_t getInt(const std::string& name) const;
        bool getBool(const std::string& name) const;

        void play();
        void pause();
        void stop();
        void reset();

        const animator::AnimatorState* getCurrentAnimatorState() const;
        const animator::AnimatorState* getPreviousAnimatorState() const;

        bool isInitialized() const { return initialized; }
        bool isPlaying() const { return state.isPlaying; }
        bool isBlending() const { return state.isBlending; }

        float getCurrentStateDuration() const;
        float getNormalizedStateTime() const;

        void forceTransitionTo(uint32_t stateId, float blendDuration = 0.25f);
        void forceTransitionTo(const std::string& stateName, float blendDuration = 0.25f);

        const animator::AnimatorData* getAnimatorData() const { return animatorData; }

        const std::vector<const animator::AnimationEvent*>& getFiredEvents() const { return firedEventsThisFrame; }

        void setRootMotionEnabled(bool enabled);
        glm::vec3 consumeRootMotionDelta();

    private:
        void evaluateTransitions();
        void startTransition(const animator::AnimatorTransition& transition);
        void updateBlending(float deltaTime);
        void evaluateCurrentPose();
        bool loadAnimationForState(uint32_t stateId);
        float getAnimationDuration(uint32_t stateId) const;

        bool shouldEvaluateExitTime(const animator::AnimatorTransition& transition,
                                    float normalizedTime, bool isLooping) const;

        void fireTriggeredEvents();
        std::vector<const animator::AnimationEvent*> firedEventsThisFrame;
    };
}
