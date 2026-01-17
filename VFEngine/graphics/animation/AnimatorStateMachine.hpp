#pragma once

#include "animator/AnimatorTypes.hpp"
#include "AnimationBlender.hpp"
#include "AnimationEvaluator.hpp"
#include "resource/Types.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <functional>

namespace animation
{
    // Callback for loading animations by path
    using AnimationLoadCallback = std::function<const resource::AnimationData*(const std::string& path)>;

    struct AnimatorStateMachineState
    {
        uint32_t currentStateId = 0;
        uint32_t previousStateId = 0;
        float stateTime = 0.0f;           // Time in current state (seconds)
        float previousStateTime = 0.0f;    // Time in previous state when transition started
        float blendWeight = 0.0f;          // 0.0 = previous state, 1.0 = current state
        float blendDuration = 0.0f;        // Total blend duration
        float blendElapsed = 0.0f;         // Elapsed blend time
        bool isBlending = false;
        bool isPlaying = true;
    };

    class AnimatorStateMachine
    {
    public:
        AnimatorStateMachine();
        ~AnimatorStateMachine() = default;

        // Initialize with animator data, skeleton data, and animation loader callback
        void initialize(const animator::AnimatorData& data, const resource::SkeletonData* skeleton, AnimationLoadCallback loadCallback);

        // Set skeleton data (can be used to update skeleton after initialization)
        void setSkeleton(const resource::SkeletonData* skeleton);

        // Update state machine (deltaTime in seconds)
        void update(float deltaTime);

        // Get the current evaluated bone matrices
        const std::vector<glm::mat4>& getBoneMatrices() const { return currentBoneMatrices; }

        // Parameter setters
        void setFloat(const std::string& name, float value);
        void setInt(const std::string& name, int32_t value);
        void setBool(const std::string& name, bool value);
        void setTrigger(const std::string& name);

        // Parameter getters
        float getFloat(const std::string& name) const;
        int32_t getInt(const std::string& name) const;
        bool getBool(const std::string& name) const;

        // State machine control
        void play();
        void pause();
        void stop();
        void reset();

        // Get current state info
        const AnimatorStateMachineState& getState() const { return state; }
        const animator::AnimatorState* getCurrentAnimatorState() const;
        const animator::AnimatorState* getPreviousAnimatorState() const;

        // Check if initialized
        bool isInitialized() const { return initialized; }
        bool isPlaying() const { return state.isPlaying; }
        bool isBlending() const { return state.isBlending; }

        // Get animation duration for current state (in seconds)
        float getCurrentStateDuration() const;

        // Get normalized time (0-1) in current state
        float getNormalizedStateTime() const;

        // Force transition to a specific state
        void forceTransitionTo(uint32_t stateId, float blendDuration = 0.25f);
        void forceTransitionTo(const std::string& stateName, float blendDuration = 0.25f);

        // Get the animator data
        const animator::AnimatorData* getAnimatorData() const { return animatorData; }

        // Get runtime parameters
        const animator::AnimatorRuntimeParameters& getParameters() const { return parameters; }

    private:
        void evaluateTransitions();
        void startTransition(const animator::AnimatorTransition& transition);
        void updateBlending(float deltaTime);
        void evaluateCurrentPose();
        bool loadAnimationForState(uint32_t stateId);
        float getAnimationDuration(uint32_t stateId) const;

        const animator::AnimatorData* animatorData = nullptr;
        const resource::SkeletonData* skeletonData = nullptr;
        AnimationLoadCallback animationLoadCallback;
        animator::AnimatorRuntimeParameters parameters;
        AnimatorStateMachineState state;

        // Animation evaluators
        AnimationEvaluator currentEvaluator;
        AnimationEvaluator previousEvaluator;

        // Cached animation data pointers (loaded via callback)
        std::unordered_map<uint32_t, const resource::AnimationData*> loadedAnimations;

        // Output bone matrices
        std::vector<glm::mat4> currentBoneMatrices;

        bool initialized = false;
    };
}
