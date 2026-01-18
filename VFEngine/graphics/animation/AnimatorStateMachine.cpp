#include "AnimatorStateMachine.hpp"
#include "print/EditorLogger.hpp"
#include <algorithm>

namespace animation
{
    AnimatorStateMachine::AnimatorStateMachine() = default;
    AnimatorStateMachine::~AnimatorStateMachine() = default;

    void AnimatorStateMachine::initialize(const animator::AnimatorData& data, const resource::SkeletonData* skeleton, AnimationLoadCallback loadCallback)
    {
        animatorData = &data;
        skeletonData = skeleton;
        animationLoadCallback = std::move(loadCallback);

        // Initialize parameters with defaults from the graph
        parameters.initializeFromGraph(data.graph);

        // Set initial state to the default state
        state = AnimatorStateMachineState{};
        state.currentStateId = data.graph.defaultStateId;
        state.isPlaying = true;

        // Clear any previously cached animations before loading new ones
        loadedAnimations.clear();

        // Load animation for the default state
        loadAnimationForState(state.currentStateId);

        initialized = true;

        // Evaluate initial pose so bone matrices are ready immediately
        evaluateCurrentPose();
    }

    void AnimatorStateMachine::setSkeleton(const resource::SkeletonData* skeleton)
    {
        skeletonData = skeleton;
    }

    void AnimatorStateMachine::update(float deltaTime)
    {
        if (!initialized || !animatorData || !state.isPlaying)
        {
            return;
        }

        // Update state time
        const animator::AnimatorState* currentState = getCurrentAnimatorState();
        if (currentState)
        {
            // Store previous normalized time for exit-time threshold detection
            float duration = getAnimationDuration(state.currentStateId);
            if (duration > 0.0f)
            {
                state.previousNormalizedTime = std::fmod(state.stateTime / duration, 1.0f);
            }

            state.stateTime += deltaTime * currentState->playbackSpeed;

            // Handle looping
            if (duration > 0.0f && !state.isBlending)
            {
                if (currentState->loop)
                {
                    while (state.stateTime >= duration)
                    {
                        state.stateTime -= duration;
                        state.currentLoopCount++;
                    }
                }
            }
        }

        // Update previous state time during blending
        if (state.isBlending)
        {
            const animator::AnimatorState* prevState = getPreviousAnimatorState();
            if (prevState)
            {
                state.previousStateTime += deltaTime * prevState->playbackSpeed;

                // Handle looping for previous state during blend
                float prevDuration = getAnimationDuration(state.previousStateId);
                if (prevDuration > 0.0f && prevState->loop)
                {
                    while (state.previousStateTime >= prevDuration)
                    {
                        state.previousStateTime -= prevDuration;
                    }
                }
            }
        }

        // Update blending
        updateBlending(deltaTime);

        // Check for transitions (only when not already blending)
        if (!state.isBlending)
        {
            evaluateTransitions();
        }

        // Evaluate the current pose
        evaluateCurrentPose();
    }

    bool AnimatorStateMachine::shouldEvaluateExitTime(const animator::AnimatorTransition& transition,
                                                        float normalizedTime, bool isLooping) const
    {
        const float exitTime = transition.exitTime;
        const float prevTime = state.previousNormalizedTime;

        if (!isLooping)
        {
            // Non-looping: evaluate once we've reached the exit time
            return normalizedTime >= exitTime;
        }

        // Looping animation: need to handle several cases carefully

        // Case 1: Already evaluated exit-time this loop iteration
        bool alreadyEvaluatedThisLoop = (state.exitTimeEvaluatedAtLoop == state.currentLoopCount) &&
                                        (prevTime >= exitTime);
        if (alreadyEvaluatedThisLoop)
        {
            return false;
        }

        // Case 2: Normal threshold crossing (previous frame was before, current is at/after)
        bool crossedThisFrame = (prevTime < exitTime) && (normalizedTime >= exitTime);
        if (crossedThisFrame)
        {
            return true;
        }

        // Case 3: Currently at or past exit time (handles multiple transitions with same exit time)
        if (normalizedTime >= exitTime)
        {
            return true;
        }

        // Case 4: Loop wrap-around - time went "backwards" because animation looped.
        // If we were below exitTime before looping, we must have crossed it.
        bool loopOccurred = normalizedTime < prevTime;
        bool wasBeforeExitTime = prevTime < exitTime;
        if (loopOccurred && wasBeforeExitTime)
        {
            return true;
        }

        // Haven't reached exit time yet
        return false;
    }

    void AnimatorStateMachine::evaluateTransitions()
    {
        if (!animatorData)
            return;

        auto transitions = animatorData->graph.getTransitionsFromState(state.currentStateId);

        for (const animator::AnimatorTransition* transition : transitions)
        {
            // Skip self-transitions (except from "Any State" which has sourceStateId == 0)
            if (transition->sourceStateId != 0 && transition->targetStateId == state.currentStateId)
            {
                continue;
            }

            // Check exit-time condition if enabled
            if (transition->hasExitTime)
            {
                float duration = getAnimationDuration(state.currentStateId);
                if (duration > 0.0f)
                {
                    float normalizedTime = std::fmod(state.stateTime / duration, 1.0f);
                    const animator::AnimatorState* animState = getCurrentAnimatorState();
                    bool isLooping = animState && animState->loop;

                    if (!shouldEvaluateExitTime(*transition, normalizedTime, isLooping))
                    {
                        continue;
                    }
                }
            }

            // Evaluate parameter conditions
            if (animator::evaluateAllConditions(transition->conditions, parameters))
            {
                // Mark exit-time as evaluated before transitioning
                if (transition->hasExitTime)
                {
                    state.exitTimeEvaluatedAtLoop = state.currentLoopCount;
                }

                startTransition(*transition);

                // Reset consumed triggers
                for (const auto& condition : transition->conditions)
                {
                    const auto* param = animatorData->graph.findParameter(condition.parameterName);
                    if (param && param->type == animator::AnimatorParameterType::Trigger)
                    {
                        parameters.resetTrigger(condition.parameterName);
                    }
                }
                break;
            }
            else if (transition->hasExitTime)
            {
                // Conditions failed but exit-time was reached - mark to prevent re-evaluation
                state.exitTimeEvaluatedAtLoop = state.currentLoopCount;
            }
        }
    }

    void AnimatorStateMachine::startTransition(const animator::AnimatorTransition& transition)
    {
        // Store previous state info
        state.previousStateId = state.currentStateId;
        state.previousStateTime = state.stateTime;

        // Load animation for previous state if not loaded
        loadAnimationForState(state.previousStateId);

        // Set new current state
        state.currentStateId = transition.targetStateId;
        state.stateTime = 0.0f;

        // Reset loop tracking for the new state
        state.currentLoopCount = 0;
        state.exitTimeEvaluatedAtLoop = 0;
        state.previousNormalizedTime = 0.0f;

        // Load animation for new state
        loadAnimationForState(state.currentStateId);

        // Setup blending
        state.blendDuration = transition.blendDuration;
        state.blendElapsed = 0.0f;
        state.blendWeight = 0.0f;
        state.isBlending = transition.blendDuration > 0.0f;
    }

    void AnimatorStateMachine::updateBlending(float deltaTime)
    {
        if (!state.isBlending)
        {
            return;
        }

        state.blendElapsed += deltaTime;

        if (state.blendDuration > 0.0f)
        {
            state.blendWeight = std::min(state.blendElapsed / state.blendDuration, 1.0f);
        }
        else
        {
            state.blendWeight = 1.0f;
        }

        // Check if blend is complete
        if (state.blendWeight >= 1.0f)
        {
            state.isBlending = false;
            state.blendWeight = 0.0f;
            state.blendDuration = 0.0f;
            state.blendElapsed = 0.0f;
        }
    }

    void AnimatorStateMachine::evaluateCurrentPose()
    {
        if (!animatorData || !skeletonData)
        {
            vfLogWarning("[AnimatorStateMachine] Cannot evaluate pose: animatorData={}, skeletonData={}",
                         animatorData != nullptr, skeletonData != nullptr);
            currentBoneMatrices.clear();
            return;
        }

        if (state.isBlending)
        {
            // Evaluate both poses and blend
            auto itPrev = loadedAnimations.find(state.previousStateId);
            auto itCurr = loadedAnimations.find(state.currentStateId);

            if (itPrev != loadedAnimations.end() && itPrev->second &&
                itCurr != loadedAnimations.end() && itCurr->second)
            {
                previousEvaluator.loadAnimation(*itPrev->second, *skeletonData);
                currentEvaluator.loadAnimation(*itCurr->second, *skeletonData);

                float prevTimeInTicks = previousEvaluator.secondsToTicks(state.previousStateTime);
                float currTimeInTicks = currentEvaluator.secondsToTicks(state.stateTime);

                std::vector<glm::mat4> prevPose = previousEvaluator.evaluatePose(prevTimeInTicks);
                std::vector<glm::mat4> currPose = currentEvaluator.evaluatePose(currTimeInTicks);

                currentBoneMatrices = AnimationBlender::blendPoses(prevPose, currPose, state.blendWeight);
            }
            else if (itCurr != loadedAnimations.end() && itCurr->second)
            {
                // Only current pose available
                currentEvaluator.loadAnimation(*itCurr->second, *skeletonData);
                float timeInTicks = currentEvaluator.secondsToTicks(state.stateTime);
                currentBoneMatrices = currentEvaluator.evaluatePose(timeInTicks);
            }
        }
        else
        {
            // Just evaluate current state
            auto it = loadedAnimations.find(state.currentStateId);
            if (it != loadedAnimations.end() && it->second)
            {
                currentEvaluator.loadAnimation(*it->second, *skeletonData);
                float timeInTicks = currentEvaluator.secondsToTicks(state.stateTime);
                currentBoneMatrices = currentEvaluator.evaluatePose(timeInTicks);
            }
            else
            {
                vfLogWarning("[AnimatorStateMachine] Animation not found for state {} (loaded={})",
                             state.currentStateId, it != loadedAnimations.end());
            }
        }
    }

    bool AnimatorStateMachine::loadAnimationForState(uint32_t stateId)
    {
        if (!animatorData || !animationLoadCallback)
        {
            return false;
        }

        // Check if already loaded
        auto it = loadedAnimations.find(stateId);
        if (it != loadedAnimations.end())
        {
            return it->second != nullptr;
        }

        // Find the state
        const animator::AnimatorState* animState = animatorData->graph.findStateById(stateId);
        if (!animState)
        {
            loadedAnimations[stateId] = nullptr;
            return false;
        }

        if (animState->animationPath.empty())
        {
            loadedAnimations[stateId] = nullptr;
            return false;
        }

        // Load animation via callback
        const resource::AnimationData* animData = animationLoadCallback(animState->animationPath);
        loadedAnimations[stateId] = animData;

        return animData != nullptr;
    }

    float AnimatorStateMachine::getAnimationDuration(uint32_t stateId) const
    {
        auto it = loadedAnimations.find(stateId);
        if (it != loadedAnimations.end() && it->second)
        {
            float tps = it->second->ticksPerSecond > 0.0f ? it->second->ticksPerSecond : 24.0f;
            return it->second->duration / tps;
        }
        return 0.0f;
    }

    float AnimatorStateMachine::getCurrentStateDuration() const
    {
        return getAnimationDuration(state.currentStateId);
    }

    float AnimatorStateMachine::getNormalizedStateTime() const
    {
        float duration = getCurrentStateDuration();
        if (duration > 0.0f)
        {
            return std::fmod(state.stateTime / duration, 1.0f);
        }
        return 0.0f;
    }

    const animator::AnimatorState* AnimatorStateMachine::getCurrentAnimatorState() const
    {
        if (!animatorData)
            return nullptr;
        return animatorData->graph.findStateById(state.currentStateId);
    }

    const animator::AnimatorState* AnimatorStateMachine::getPreviousAnimatorState() const
    {
        if (!animatorData)
            return nullptr;
        return animatorData->graph.findStateById(state.previousStateId);
    }

    void AnimatorStateMachine::setFloat(const std::string& name, float value)
    {
        parameters.setFloat(name, value);
    }

    void AnimatorStateMachine::setInt(const std::string& name, int32_t value)
    {
        parameters.setInt(name, value);
    }

    void AnimatorStateMachine::setBool(const std::string& name, bool value)
    {
        parameters.setBool(name, value);
    }

    void AnimatorStateMachine::setTrigger(const std::string& name)
    {
        parameters.setTrigger(name);
    }

    float AnimatorStateMachine::getFloat(const std::string& name) const
    {
        return parameters.getFloat(name);
    }

    int32_t AnimatorStateMachine::getInt(const std::string& name) const
    {
        return parameters.getInt(name);
    }

    bool AnimatorStateMachine::getBool(const std::string& name) const
    {
        return parameters.getBool(name);
    }

    void AnimatorStateMachine::play()
    {
        state.isPlaying = true;
    }

    void AnimatorStateMachine::pause()
    {
        state.isPlaying = false;
    }

    void AnimatorStateMachine::stop()
    {
        state.isPlaying = false;
        state.stateTime = 0.0f;
        state.isBlending = false;
        state.blendWeight = 0.0f;
        state.blendDuration = 0.0f;
        state.blendElapsed = 0.0f;
    }

    void AnimatorStateMachine::reset()
    {
        if (!animatorData)
            return;

        // Reset to initial state
        state = AnimatorStateMachineState{};
        state.currentStateId = animatorData->graph.defaultStateId;
        state.isPlaying = true;

        // Reset parameters to defaults
        parameters.initializeFromGraph(animatorData->graph);

        // Reload animation for default state
        loadAnimationForState(state.currentStateId);
    }

    void AnimatorStateMachine::forceTransitionTo(uint32_t stateId, float blendDuration)
    {
        if (!animatorData)
            return;

        // Verify state exists
        const animator::AnimatorState* targetState = animatorData->graph.findStateById(stateId);
        if (!targetState)
        {
            return;
        }

        // Create a temporary transition
        animator::AnimatorTransition tempTransition;
        tempTransition.sourceStateId = state.currentStateId;
        tempTransition.targetStateId = stateId;
        tempTransition.blendDuration = blendDuration;

        startTransition(tempTransition);
    }

    void AnimatorStateMachine::forceTransitionTo(const std::string& stateName, float blendDuration)
    {
        if (!animatorData)
            return;

        const animator::AnimatorState* targetState = animatorData->graph.findStateByName(stateName);
        if (!targetState)
        {
            return;
        }

        forceTransitionTo(targetState->id, blendDuration);
    }
}
