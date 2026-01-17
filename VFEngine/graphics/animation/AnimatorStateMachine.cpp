#include "AnimatorStateMachine.hpp"
#include "print/Logger.hpp"
#include <algorithm>

namespace animation
{
    AnimatorStateMachine::AnimatorStateMachine()
    {
    }

    void AnimatorStateMachine::initialize(const animator::AnimatorData& data, AnimationLoadCallback loadCallback)
    {
        animatorData = &data;
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

        loggerInfo("AnimatorStateMachine initialized with {} states, {} transitions",
                   data.graph.states.size(), data.graph.transitions.size());
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
            float oldTime = state.stateTime;
            state.stateTime += deltaTime * currentState->playbackSpeed;

            // Log occasionally to verify animation time is advancing
            static int logCounter = 0;
            if (logCounter++ % 300 == 0)
            {
                float duration = getAnimationDuration(state.currentStateId);
                loggerInfo("[AnimatorStateMachine] stateTime: {:.3f} -> {:.3f} (duration={:.3f}, speed={:.1f}, deltaTime={:.4f})",
                    oldTime, state.stateTime, duration, currentState->playbackSpeed, deltaTime);
            }

            // Handle looping
            float duration = getAnimationDuration(state.currentStateId);
            if (duration > 0.0f && !state.isBlending)
            {
                if (currentState->loop)
                {
                    while (state.stateTime >= duration)
                    {
                        state.stateTime -= duration;
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

    void AnimatorStateMachine::evaluateTransitions()
    {
        if (!animatorData)
            return;

        // Get all valid transitions from current state
        auto transitions = animatorData->graph.getTransitionsFromState(state.currentStateId);

        for (const animator::AnimatorTransition* transition : transitions)
        {
            // Skip transitions to the same state (unless it's an "Any State" transition)
            if (transition->sourceStateId != 0 && transition->targetStateId == state.currentStateId)
            {
                continue;
            }

            // Check exit time if enabled
            if (transition->hasExitTime)
            {
                float duration = getAnimationDuration(state.currentStateId);
                if (duration > 0.0f)
                {
                    float normalizedTime = state.stateTime / duration;
                    // Use fmod for looping animations
                    normalizedTime = std::fmod(normalizedTime, 1.0f);
                    if (normalizedTime < transition->exitTime)
                    {
                        continue;
                    }
                }
            }

            // Evaluate conditions
            if (animator::evaluateAllConditions(transition->conditions, parameters))
            {
                startTransition(*transition);

                // Reset triggers that were consumed
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

        // Load animation for new state
        loadAnimationForState(state.currentStateId);

        // Setup blending
        state.blendDuration = transition.blendDuration;
        state.blendElapsed = 0.0f;
        state.blendWeight = 0.0f;
        state.isBlending = transition.blendDuration > 0.0f;

        loggerInfo("Animator transition: {} -> {}",
                   state.previousStateId, state.currentStateId);
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
        if (!animatorData)
        {
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
                previousEvaluator.loadAnimation(*itPrev->second);
                currentEvaluator.loadAnimation(*itCurr->second);

                float prevTimeInTicks = previousEvaluator.secondsToTicks(state.previousStateTime);
                float currTimeInTicks = currentEvaluator.secondsToTicks(state.stateTime);

                std::vector<glm::mat4> prevPose = previousEvaluator.evaluatePose(prevTimeInTicks);
                std::vector<glm::mat4> currPose = currentEvaluator.evaluatePose(currTimeInTicks);

                currentBoneMatrices = AnimationBlender::blendPoses(prevPose, currPose, state.blendWeight);
            }
            else if (itCurr != loadedAnimations.end() && itCurr->second)
            {
                // Only current pose available
                currentEvaluator.loadAnimation(*itCurr->second);
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
                currentEvaluator.loadAnimation(*it->second);
                float timeInTicks = currentEvaluator.secondsToTicks(state.stateTime);
                currentBoneMatrices = currentEvaluator.evaluatePose(timeInTicks);

                // Log occasionally to verify bone matrices are changing
                static int evalLogCounter = 0;
                if (evalLogCounter++ % 300 == 0 && !currentBoneMatrices.empty())
                {
                    const auto& m = currentBoneMatrices[0];
                    loggerInfo("[AnimatorStateMachine] evaluatePose at time={:.3f} ticks, bone[0]=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        timeInTicks, m[3][0], m[3][1], m[3][2], m[3][3]);
                }
            }
        }
    }

    bool AnimatorStateMachine::loadAnimationForState(uint32_t stateId)
    {
        loggerInfo("[AnimatorStateMachine] loadAnimationForState called for stateId: {}", stateId);

        if (!animatorData || !animationLoadCallback)
        {
            loggerWarning("[AnimatorStateMachine] No animatorData or callback!");
            return false;
        }

        // Check if already loaded
        auto it = loadedAnimations.find(stateId);
        if (it != loadedAnimations.end())
        {
            loggerInfo("[AnimatorStateMachine] Animation already loaded for state {}, has data: {}",
                stateId, it->second != nullptr);
            return it->second != nullptr;
        }

        // Find the state
        const animator::AnimatorState* animState = animatorData->graph.findStateById(stateId);
        if (!animState)
        {
            loggerWarning("[AnimatorStateMachine] State {} not found in graph!", stateId);
            loadedAnimations[stateId] = nullptr;
            return false;
        }

        if (animState->animationPath.empty())
        {
            loggerWarning("[AnimatorStateMachine] State '{}' (id={}) has empty animation path!",
                animState->name, stateId);
            loadedAnimations[stateId] = nullptr;
            return false;
        }

        loggerInfo("[AnimatorStateMachine] Loading animation '{}' for state '{}'",
            animState->animationPath, animState->name);

        // Load animation via callback
        const resource::AnimationData* animData = animationLoadCallback(animState->animationPath);
        loadedAnimations[stateId] = animData;

        if (animData)
        {
            loggerInfo("[AnimatorStateMachine] Successfully loaded animation '{}' for state '{}', bones: {}",
                       animState->animationPath, animState->name, animData->channels.size());
        }
        else
        {
            loggerWarning("[AnimatorStateMachine] Failed to load animation '{}' for state '{}'",
                          animState->animationPath, animState->name);
        }

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
            loggerWarning("Cannot force transition to non-existent state {}", stateId);
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
            loggerWarning("Cannot force transition to non-existent state '{}'", stateName);
            return;
        }

        forceTransitionTo(targetState->id, blendDuration);
    }
}
