#pragma once

#include "AnimationExport.hpp"
#include "animator/AnimatorTypes.hpp"
#include "animator/AnimationEventTypes.hpp"
#include "animator/SocketTypes.hpp"
#include "AnimationBlender.hpp"
#include "AnimationEvaluator.hpp"
#include "BlendTree.hpp"
#include "resource/Types.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <unordered_set>

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

#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_ANIMATION_API AnimatorStateMachine
    {
    private:
        const animator::AnimatorData* animatorData = nullptr;
        const animator::AnimatorGraph* activeGraph = nullptr;
        const resource::SkeletonData* skeletonData = nullptr;
        const RetargetContext* retargetContext = nullptr; // null = native skeleton (VK-910)
        AnimationLoadCallback animationLoadCallback;
        animator::AnimatorRuntimeParameters ownedParameters;
        animator::AnimatorRuntimeParameters* parameters = nullptr;
        AnimatorStateMachineState state;

        AnimationEvaluator currentEvaluator;
        AnimationEvaluator previousEvaluator;
        BlendTreeEvaluator blendTreeEvaluator;

        std::unordered_map<uint32_t, const resource::AnimationData*> loadedAnimations;
        std::unordered_set<uint32_t> loadedBlendTreeStates;

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

        void initialize(const animator::AnimatorData& data, const resource::SkeletonData* skeleton, AnimationLoadCallback loadCallback,
                        const RetargetContext* retarget = nullptr);
        void initializeFromGraph(const animator::AnimatorGraph& graph, const resource::SkeletonData* skeleton,
                                 AnimationLoadCallback loadCallback, animator::AnimatorRuntimeParameters* externalParams = nullptr,
                                 const RetargetContext* retarget = nullptr);

        void update(float deltaTime);

        const std::vector<glm::mat4>& getBoneMatrices() const { return currentBoneMatrices; }
        std::vector<glm::mat4>& getMutableBoneMatrices() { return currentBoneMatrices; }

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

        const AnimatorStateMachineState& getMachineState() const { return state; }
        void setMachineState(const AnimatorStateMachineState& newState);
        bool isRootMotionEnabled() const { return rootMotionEnabled; }

        float getCurrentStateDuration() const;
        float getNormalizedStateTime() const;

        // Seconds-per-frame of the current state's clip (1 / ticksPerSecond, i.e. one source
        // animation frame). Falls back to 1/30s when no clip is resolvable. Used by the prefab-
        // rig preview's frame-step scrub (VK-1433); does NOT advance time.
        float getCurrentClipFrameDuration() const;

        // Editor-only absolute seek (VK-1433 prefab-rig preview scrub). Sets the current state's
        // stateTime to clamp(t,0,1)*duration, cancels any in-progress blend, and re-evaluates the
        // pose at that time — WITHOUT firing transitions/events. Play (update()) is unchanged.
        void setNormalizedStateTime(float t);

        void forceTransitionTo(uint32_t stateId, float blendDuration = 0.25f);
        void forceTransitionTo(const std::string& stateName, float blendDuration = 0.25f);

        const animator::AnimatorData* getAnimatorData() const { return animatorData; }
        const animator::AnimatorGraph* getActiveGraph() const { return activeGraph; }

        const std::vector<const animator::AnimationEvent*>& getFiredEvents() const { return firedEventsThisFrame; }

        void setRootMotionEnabled(bool enabled);
        glm::vec3 consumeRootMotionDelta();

    private:
        void evaluateTransitions();
        bool checkTransitionConditions(const animator::AnimatorTransition& transition) const;
        void executeTransition(const animator::AnimatorTransition& transition);
        void startTransition(const animator::AnimatorTransition& transition);
        void updateBlending(float deltaTime);
        void advanceStateTime(float deltaTime);
        void advancePreviousStateTime(float deltaTime);
        void evaluateCurrentPose();
        void evaluateBlendingPose(glm::vec3& outRootPosition);
        std::vector<glm::mat4> evaluateStatePose(uint32_t stateId, float time,
                                                   AnimationEvaluator& evaluator,
                                                   glm::vec3* outRootPos) const;
        void updateRootMotionDelta(const glm::vec3& currentRootPosition);
        bool loadAnimationForState(uint32_t stateId);
        void loadBlendTreeAnimations(const animator::AnimatorState& state);
        float getAnimationDuration(uint32_t stateId) const;
        bool hasBlendTree(uint32_t stateId) const;
        std::vector<glm::mat4> evaluateBlendTreePose(const animator::AnimatorState& state, float time) const;
        std::vector<glm::mat4> evaluateBlendTreePose(const animator::AnimatorState& state, float time, glm::vec3& outRootPosition) const;

        bool shouldEvaluateExitTime(const animator::AnimatorTransition& transition,
                                    float normalizedTime, bool isLooping) const;

        void fireTriggeredEvents();
        std::vector<const animator::AnimationEvent*> firedEventsThisFrame;
    };
#pragma warning(pop)
}
