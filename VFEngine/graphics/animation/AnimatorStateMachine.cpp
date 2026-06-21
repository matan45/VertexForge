#include "print/Log.hpp"
#include "AnimatorStateMachine.hpp"
#include <algorithm>

namespace animation
{
    AnimatorStateMachine::AnimatorStateMachine() = default;
    AnimatorStateMachine::~AnimatorStateMachine() = default;

    void AnimatorStateMachine::initialize(const animator::AnimatorData& data, const resource::SkeletonData* skeleton, AnimationLoadCallback loadCallback,
                                          const RetargetContext* retarget)
    {
        animatorData = &data;
        activeGraph = &data.graph;
        skeletonData = skeleton;
        retargetContext = retarget;
        animationLoadCallback = std::move(loadCallback);

        ownedParameters.initializeFromGraph(data.graph);
        parameters = &ownedParameters;

        state = AnimatorStateMachineState{};
        state.currentStateId = data.graph.defaultStateId;
        state.isPlaying = true;

        loadedAnimations.clear();
        loadedBlendTreeStates.clear();

        loadAnimationForState(state.currentStateId);

        initialized = true;

        evaluateCurrentPose();
    }

    void AnimatorStateMachine::initializeFromGraph(const animator::AnimatorGraph& graph, const resource::SkeletonData* skeleton,
                                                    AnimationLoadCallback loadCallback, animator::AnimatorRuntimeParameters* externalParams,
                                                    const RetargetContext* retarget)
    {
        animatorData = nullptr;
        activeGraph = &graph;
        skeletonData = skeleton;
        retargetContext = retarget;
        animationLoadCallback = std::move(loadCallback);

        if (externalParams)
        {
            parameters = externalParams;
        }
        else
        {
            ownedParameters.initializeFromGraph(graph);
            parameters = &ownedParameters;
        }

        state = AnimatorStateMachineState{};
        state.currentStateId = graph.defaultStateId;
        state.isPlaying = true;

        loadedAnimations.clear();
        loadedBlendTreeStates.clear();

        loadAnimationForState(state.currentStateId);

        initialized = true;

        evaluateCurrentPose();
    }

    void AnimatorStateMachine::advanceStateTime(float deltaTime)
    {
        const animator::AnimatorState* currentState = getCurrentAnimatorState();
        if (!currentState)
            return;

        float duration = getAnimationDuration(state.currentStateId);
        if (duration > 0.0f)
        {
            state.previousNormalizedTime = std::fmod(state.stateTime / duration, 1.0f);
        }

        state.stateTime += deltaTime * currentState->playbackSpeed;

        if (duration > 0.0f && !state.isBlending && currentState->loop)
        {
            while (state.stateTime >= duration)
            {
                state.stateTime -= duration;
                state.currentLoopCount++;
            }
        }
    }

    void AnimatorStateMachine::advancePreviousStateTime(float deltaTime)
    {
        const animator::AnimatorState* prevState = getPreviousAnimatorState();
        if (!prevState)
            return;

        state.previousStateTime += deltaTime * prevState->playbackSpeed;

        float prevDuration = getAnimationDuration(state.previousStateId);
        if (prevDuration > 0.0f && prevState->loop)
        {
            while (state.previousStateTime >= prevDuration)
            {
                state.previousStateTime -= prevDuration;
            }
        }
    }

    void AnimatorStateMachine::update(float deltaTime)
    {
        if (!initialized || !activeGraph || !state.isPlaying)
            return;

        advanceStateTime(deltaTime);

        if (state.isBlending)
            advancePreviousStateTime(deltaTime);

        updateBlending(deltaTime);

        if (!state.isBlending)
            evaluateTransitions();

        fireTriggeredEvents();
        evaluateCurrentPose();
    }

    void AnimatorStateMachine::fireTriggeredEvents()
    {
        firedEventsThisFrame.clear();

        if (!activeGraph)
            return;

        const animator::AnimatorState* currentState = getCurrentAnimatorState();
        if (!currentState)
            return;

        float duration = getAnimationDuration(state.currentStateId);
        if (duration <= 0.0f)
            return;

        const float currentNormalized = std::fmod(state.stateTime / duration, 1.0f);
        const float prevNormalized = state.previousNormalizedTime;

        const bool looped = (state.currentLoopCount != eventLastLoopCount) || (currentNormalized < prevNormalized);

        // Shared crossing test: fires an event when the playhead crosses its normalizedTime this
        // frame, with loop-wrap handling identical for both event sources below.
        const auto shouldFire = [&](const animator::AnimationEvent& event) -> bool
        {
            if (looped)
            {
                return (event.normalizedTime > prevNormalized) ||
                       (event.normalizedTime <= currentNormalized);
            }
            return (event.normalizedTime > prevNormalized) &&
                   (event.normalizedTime <= currentNormalized);
        };

        // Source A: state-level events authored directly on the animator state.
        for (const auto& event : currentState->events)
        {
            if (shouldFire(event))
                firedEventsThisFrame.push_back(&event);
        }

        // Source B: timeline notify events authored on the .vfAnim clip itself (VK-1425). Only
        // single-clip states have a canonical clip; blend-tree states keep firing state events only.
        // Pointers reference the resource-cached AnimationData, stable for the state machine lifetime.
        if (!currentState->blendTree.has_value())
        {
            auto it = loadedAnimations.find(state.currentStateId);
            if (it != loadedAnimations.end() && it->second)
            {
                for (const auto& event : it->second->events)
                {
                    if (shouldFire(event))
                        firedEventsThisFrame.push_back(&event);
                }
            }
        }

        eventLastLoopCount = state.currentLoopCount;
    }

    bool AnimatorStateMachine::shouldEvaluateExitTime(const animator::AnimatorTransition& transition,
                                                        float normalizedTime, bool isLooping) const
    {
        const float exitTime = transition.exitTime;
        const float prevTime = state.previousNormalizedTime;

        if (!isLooping)
        {
            return normalizedTime >= exitTime;
        }

        bool alreadyEvaluatedThisLoop = (state.exitTimeEvaluatedAtLoop == state.currentLoopCount) &&
                                        (prevTime >= exitTime);
        if (alreadyEvaluatedThisLoop)
        {
            return false;
        }

        bool crossedThisFrame = (prevTime < exitTime) && (normalizedTime >= exitTime);
        if (crossedThisFrame)
        {
            return true;
        }

        if (normalizedTime >= exitTime)
        {
            return true;
        }

        bool loopOccurred = normalizedTime < prevTime;
        bool wasBeforeExitTime = prevTime < exitTime;
        if (loopOccurred && wasBeforeExitTime)
        {
            return true;
        }

        return false;
    }

    bool AnimatorStateMachine::checkTransitionConditions(const animator::AnimatorTransition& transition) const
    {
        if (transition.sourceStateId != 0 && transition.targetStateId == state.currentStateId)
        {
            return false;
        }

        if (transition.hasExitTime)
        {
            float duration = getAnimationDuration(state.currentStateId);
            if (duration > 0.0f)
            {
                float normalizedTime = std::fmod(state.stateTime / duration, 1.0f);
                const animator::AnimatorState* animState = getCurrentAnimatorState();
                bool isLooping = animState && animState->loop;

                if (!shouldEvaluateExitTime(transition, normalizedTime, isLooping))
                {
                    return false;
                }
            }
        }

        return animator::evaluateAllConditions(transition.conditions, *parameters);
    }

    void AnimatorStateMachine::executeTransition(const animator::AnimatorTransition& transition)
    {
        if (transition.hasExitTime)
        {
            state.exitTimeEvaluatedAtLoop = state.currentLoopCount;
        }

        startTransition(transition);

        for (const auto& condition : transition.conditions)
        {
            const auto* param = activeGraph->findParameter(condition.parameterName);
            if (param && param->type == animator::AnimatorParameterType::Trigger)
            {
                parameters->resetTrigger(condition.parameterName);
            }
        }
    }

    void AnimatorStateMachine::evaluateTransitions()
    {
        if (!activeGraph)
            return;

        auto transitions = activeGraph->getTransitionsFromState(state.currentStateId);

        for (const animator::AnimatorTransition* transition : transitions)
        {
            if (checkTransitionConditions(*transition))
            {
                executeTransition(*transition);
                break;
            }
            else if (transition->hasExitTime)
            {
                state.exitTimeEvaluatedAtLoop = state.currentLoopCount;
            }
        }
    }

    void AnimatorStateMachine::startTransition(const animator::AnimatorTransition& transition)
    {
        state.previousStateId = state.currentStateId;
        state.previousStateTime = state.stateTime;

        loadAnimationForState(state.previousStateId);

        state.currentStateId = transition.targetStateId;
        state.stateTime = 0.0f;

        state.currentLoopCount = 0;
        state.exitTimeEvaluatedAtLoop = 0;
        state.previousNormalizedTime = 0.0f;
        // Keep the event loop-tracking in step with the reset loop count, otherwise the first frame
        // of the new state sees currentLoopCount(0) != eventLastLoopCount(>0) and spuriously treats
        // it as a wrap, firing every event with normalizedTime > 0 (VK-1425).
        eventLastLoopCount = 0;

        loadAnimationForState(state.currentStateId);

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

        if (state.blendWeight >= 1.0f)
        {
            state.isBlending = false;
            state.blendWeight = 0.0f;
            state.blendDuration = 0.0f;
            state.blendElapsed = 0.0f;
        }
    }

    std::vector<glm::mat4> AnimatorStateMachine::evaluateStatePose(
        uint32_t stateId, float time, AnimationEvaluator& evaluator, glm::vec3* outRootPos) const
    {
        const animator::AnimatorState* animState = activeGraph->findStateById(stateId);

        if (animState && hasBlendTree(stateId))
        {
            if (outRootPos)
                return evaluateBlendTreePose(*animState, time, *outRootPos);
            return evaluateBlendTreePose(*animState, time);
        }

        auto it = loadedAnimations.find(stateId);
        if (it == loadedAnimations.end() || !it->second)
            return {};

        evaluator.loadAnimation(*it->second, *skeletonData, retargetContext);
        float timeInTicks = evaluator.secondsToTicks(time);

        if (outRootPos)
            return evaluator.evaluatePose(timeInTicks, *outRootPos);
        return evaluator.evaluatePose(timeInTicks);
    }

    void AnimatorStateMachine::updateRootMotionDelta(const glm::vec3& currentRootPosition)
    {
        if (rootMotionFirstFrame)
        {
            rootMotionDelta = glm::vec3(0.0f);
            rootMotionFirstFrame = false;
        }
        else if (state.currentLoopCount != rootMotionLastLoopCount)
        {
            rootMotionDelta = glm::vec3(0.0f);
        }
        else
        {
            rootMotionDelta = currentRootPosition - previousRootPosition;
        }

        previousRootPosition = currentRootPosition;
        rootMotionLastLoopCount = state.currentLoopCount;
    }

    void AnimatorStateMachine::evaluateBlendingPose(glm::vec3& outRootPosition)
    {
        glm::vec3 prevRoot{0.0f}, currRoot{0.0f};
        glm::vec3* prevRootPtr = rootMotionEnabled ? &prevRoot : nullptr;
        glm::vec3* currRootPtr = rootMotionEnabled ? &currRoot : nullptr;

        auto prevPose = evaluateStatePose(state.previousStateId, state.previousStateTime,
                                           previousEvaluator, prevRootPtr);
        auto currPose = evaluateStatePose(state.currentStateId, state.stateTime,
                                           currentEvaluator, currRootPtr);

        if (!prevPose.empty() && !currPose.empty())
        {
            currentBoneMatrices = AnimationBlender::blendPoses(prevPose, currPose, state.blendWeight);
            if (rootMotionEnabled)
                outRootPosition = glm::mix(prevRoot, currRoot, state.blendWeight);
        }
        else if (!currPose.empty())
        {
            currentBoneMatrices = currPose;
            if (rootMotionEnabled)
                outRootPosition = currRoot;
        }
    }

    void AnimatorStateMachine::evaluateCurrentPose()
    {
        if (!activeGraph || !skeletonData)
        {
            vfLogWarning("[AnimatorStateMachine] Cannot evaluate pose: activeGraph={}, skeletonData={}",
                         activeGraph != nullptr, skeletonData != nullptr);
            currentBoneMatrices.clear();
            return;
        }

        glm::vec3 currentRootPosition{0.0f};

        if (state.isBlending)
        {
            evaluateBlendingPose(currentRootPosition);
        }
        else
        {
            glm::vec3* rootPtr = rootMotionEnabled ? &currentRootPosition : nullptr;
            auto pose = evaluateStatePose(state.currentStateId, state.stateTime,
                                           currentEvaluator, rootPtr);
            if (!pose.empty())
            {
                currentBoneMatrices = std::move(pose);
            }
            else
            {
                vfLogWarning("[AnimatorStateMachine] Animation not found for state {}",
                             state.currentStateId);
            }
        }

        if (rootMotionEnabled)
            updateRootMotionDelta(currentRootPosition);
    }

}
