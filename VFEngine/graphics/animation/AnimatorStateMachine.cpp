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

        parameters.initializeFromGraph(data.graph);

        state = AnimatorStateMachineState{};
        state.currentStateId = data.graph.defaultStateId;
        state.isPlaying = true;

        loadedAnimations.clear();

        loadAnimationForState(state.currentStateId);

        initialized = true;

        evaluateCurrentPose();
    }

    void AnimatorStateMachine::update(float deltaTime)
    {
        if (!initialized || !animatorData || !state.isPlaying)
        {
            return;
        }

        const animator::AnimatorState* currentState = getCurrentAnimatorState();
        if (currentState)
        {
            float duration = getAnimationDuration(state.currentStateId);
            if (duration > 0.0f)
            {
                state.previousNormalizedTime = std::fmod(state.stateTime / duration, 1.0f);
            }

            state.stateTime += deltaTime * currentState->playbackSpeed;

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

        if (state.isBlending)
        {
            const animator::AnimatorState* prevState = getPreviousAnimatorState();
            if (prevState)
            {
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
        }

        updateBlending(deltaTime);

        if (!state.isBlending)
        {
            evaluateTransitions();
        }

        fireTriggeredEvents();

        evaluateCurrentPose();
    }

    void AnimatorStateMachine::fireTriggeredEvents()
    {
        firedEventsThisFrame.clear();

        if (!animatorData)
            return;

        const animator::AnimatorState* currentState = getCurrentAnimatorState();
        if (!currentState || currentState->events.empty())
            return;

        float duration = getAnimationDuration(state.currentStateId);
        if (duration <= 0.0f)
            return;

        float currentNormalized = std::fmod(state.stateTime / duration, 1.0f);
        float prevNormalized = state.previousNormalizedTime;

        bool looped = (state.currentLoopCount != eventLastLoopCount) || (currentNormalized < prevNormalized);

        for (const auto& event : currentState->events)
        {
            bool shouldFire = false;

            if (looped)
            {
                shouldFire = (event.normalizedTime > prevNormalized) ||
                             (event.normalizedTime <= currentNormalized);
            }
            else
            {
                shouldFire = (event.normalizedTime > prevNormalized) &&
                             (event.normalizedTime <= currentNormalized);
            }

            if (shouldFire)
            {
                firedEventsThisFrame.push_back(&event);
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

    void AnimatorStateMachine::evaluateTransitions()
    {
        if (!animatorData)
            return;

        auto transitions = animatorData->graph.getTransitionsFromState(state.currentStateId);

        for (const animator::AnimatorTransition* transition : transitions)
        {
            if (transition->sourceStateId != 0 && transition->targetStateId == state.currentStateId)
            {
                continue;
            }

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

            if (animator::evaluateAllConditions(transition->conditions, parameters))
            {
                if (transition->hasExitTime)
                {
                    state.exitTimeEvaluatedAtLoop = state.currentLoopCount;
                }

                startTransition(*transition);

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

    void AnimatorStateMachine::evaluateCurrentPose()
    {
        if (!animatorData || !skeletonData)
        {
            vfLogWarning("[AnimatorStateMachine] Cannot evaluate pose: animatorData={}, skeletonData={}",
                         animatorData != nullptr, skeletonData != nullptr);
            currentBoneMatrices.clear();
            return;
        }

        glm::vec3 currentRootPosition{0.0f};

        if (state.isBlending)
        {
            auto itPrev = loadedAnimations.find(state.previousStateId);
            auto itCurr = loadedAnimations.find(state.currentStateId);

            if (itPrev != loadedAnimations.end() && itPrev->second &&
                itCurr != loadedAnimations.end() && itCurr->second)
            {
                previousEvaluator.loadAnimation(*itPrev->second, *skeletonData);
                currentEvaluator.loadAnimation(*itCurr->second, *skeletonData);

                float prevTimeInTicks = previousEvaluator.secondsToTicks(state.previousStateTime);
                float currTimeInTicks = currentEvaluator.secondsToTicks(state.stateTime);

                if (rootMotionEnabled)
                {
                    glm::vec3 prevRoot, currRoot;
                    std::vector<glm::mat4> prevPose = previousEvaluator.evaluatePose(prevTimeInTicks, prevRoot);
                    std::vector<glm::mat4> currPose = currentEvaluator.evaluatePose(currTimeInTicks, currRoot);
                    currentBoneMatrices = AnimationBlender::blendPoses(prevPose, currPose, state.blendWeight);
                    currentRootPosition = glm::mix(prevRoot, currRoot, state.blendWeight);
                }
                else
                {
                    std::vector<glm::mat4> prevPose = previousEvaluator.evaluatePose(prevTimeInTicks);
                    std::vector<glm::mat4> currPose = currentEvaluator.evaluatePose(currTimeInTicks);
                    currentBoneMatrices = AnimationBlender::blendPoses(prevPose, currPose, state.blendWeight);
                }
            }
            else if (itCurr != loadedAnimations.end() && itCurr->second)
            {
                currentEvaluator.loadAnimation(*itCurr->second, *skeletonData);
                float timeInTicks = currentEvaluator.secondsToTicks(state.stateTime);
                if (rootMotionEnabled)
                    currentBoneMatrices = currentEvaluator.evaluatePose(timeInTicks, currentRootPosition);
                else
                    currentBoneMatrices = currentEvaluator.evaluatePose(timeInTicks);
            }
        }
        else
        {
            auto it = loadedAnimations.find(state.currentStateId);
            if (it != loadedAnimations.end() && it->second)
            {
                currentEvaluator.loadAnimation(*it->second, *skeletonData);
                float timeInTicks = currentEvaluator.secondsToTicks(state.stateTime);
                if (rootMotionEnabled)
                    currentBoneMatrices = currentEvaluator.evaluatePose(timeInTicks, currentRootPosition);
                else
                    currentBoneMatrices = currentEvaluator.evaluatePose(timeInTicks);
            }
            else
            {
                vfLogWarning("[AnimatorStateMachine] Animation not found for state {} (loaded={})",
                             state.currentStateId, it != loadedAnimations.end());
            }
        }

        if (rootMotionEnabled)
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
    }

    bool AnimatorStateMachine::loadAnimationForState(uint32_t stateId)
    {
        if (!animatorData || !animationLoadCallback)
        {
            return false;
        }

        auto it = loadedAnimations.find(stateId);
        if (it != loadedAnimations.end())
        {
            return it->second != nullptr;
        }

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
}
