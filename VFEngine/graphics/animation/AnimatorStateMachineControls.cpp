#include "AnimatorStateMachine.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace animation
{
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

    float AnimatorStateMachine::getCurrentClipFrameDuration() const
    {
        // One source frame = one tick, so seconds-per-frame = 1 / ticksPerSecond. Mirror the
        // tps fallback (24.0f) used by getAnimationDuration for blend-tree / single-clip states,
        // but expose the per-frame step in the same seconds unit as state.stateTime.
        if (activeGraph)
        {
            const animator::AnimatorState* animState = activeGraph->findStateById(state.currentStateId);
            if (animState && animState->blendTree.has_value() && !animState->blendTree->entries.empty())
            {
                for (const auto& entry : animState->blendTree->entries)
                {
                    if (entry.animationRef.isValid() && animationLoadCallback)
                    {
                        const resource::AnimationData* data = animationLoadCallback(entry.animationRef.resolve());
                        if (data)
                        {
                            float tps = data->ticksPerSecond > 0.0f ? data->ticksPerSecond : 24.0f;
                            return 1.0f / tps;
                        }
                    }
                }
            }
        }

        auto it = loadedAnimations.find(state.currentStateId);
        if (it != loadedAnimations.end() && it->second)
        {
            float tps = it->second->ticksPerSecond > 0.0f ? it->second->ticksPerSecond : 24.0f;
            return 1.0f / tps;
        }

        // No resolvable clip (e.g. a state with no animation): fall back to 30 FPS.
        return 1.0f / 30.0f;
    }

    void AnimatorStateMachine::setNormalizedStateTime(float t)
    {
        if (!initialized || !activeGraph)
            return;

        const float clamped = std::clamp(t, 0.0f, 1.0f);
        const float duration = getCurrentStateDuration();

        // Cancel any in-progress blend so the seek maps to the current state alone (a settled
        // state). This mirrors the post-blend resting state without advancing time.
        state.isBlending = false;
        state.blendWeight = 0.0f;
        state.blendDuration = 0.0f;
        state.blendElapsed = 0.0f;

        // duration is in seconds (data->duration / ticksPerSecond); stateTime is seconds too.
        state.stateTime = (duration > 0.0f) ? clamped * duration : 0.0f;

        // Keep the loop/event bookkeeping consistent with a non-wrapping playhead at this time so
        // a later update() does not see a spurious wrap.
        state.previousNormalizedTime = clamped;

        // Re-evaluate the current pose at the seeked time WITHOUT firing transitions/events.
        evaluateCurrentPose();
    }

    const animator::AnimatorState* AnimatorStateMachine::getCurrentAnimatorState() const
    {
        if (!activeGraph)
            return nullptr;
        return activeGraph->findStateById(state.currentStateId);
    }

    const animator::AnimatorState* AnimatorStateMachine::getPreviousAnimatorState() const
    {
        if (!activeGraph)
            return nullptr;
        return activeGraph->findStateById(state.previousStateId);
    }

    void AnimatorStateMachine::setFloat(const std::string& name, float value)
    {
        parameters->setFloat(name, value);
    }

    void AnimatorStateMachine::setInt(const std::string& name, int32_t value)
    {
        parameters->setInt(name, value);
    }

    void AnimatorStateMachine::setBool(const std::string& name, bool value)
    {
        parameters->setBool(name, value);
    }

    void AnimatorStateMachine::setTrigger(const std::string& name)
    {
        parameters->setTrigger(name);
    }

    float AnimatorStateMachine::getFloat(const std::string& name) const
    {
        return parameters->getFloat(name);
    }

    int32_t AnimatorStateMachine::getInt(const std::string& name) const
    {
        return parameters->getInt(name);
    }

    bool AnimatorStateMachine::getBool(const std::string& name) const
    {
        return parameters->getBool(name);
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
        if (!activeGraph)
            return;

        state = AnimatorStateMachineState{};
        state.currentStateId = activeGraph->defaultStateId;
        state.isPlaying = true;

        parameters->initializeFromGraph(*activeGraph);

        loadAnimationForState(state.currentStateId);
    }

    void AnimatorStateMachine::forceTransitionTo(uint32_t stateId, float blendDuration)
    {
        if (!activeGraph)
            return;

        const animator::AnimatorState* targetState = activeGraph->findStateById(stateId);
        if (!targetState)
        {
            return;
        }

        animator::AnimatorTransition tempTransition;
        tempTransition.sourceStateId = state.currentStateId;
        tempTransition.targetStateId = stateId;
        tempTransition.blendDuration = blendDuration;

        startTransition(tempTransition);
    }

    void AnimatorStateMachine::forceTransitionTo(const std::string& stateName, float blendDuration)
    {
        if (!activeGraph)
            return;

        const animator::AnimatorState* targetState = activeGraph->findStateByName(stateName);
        if (!targetState)
        {
            return;
        }

        forceTransitionTo(targetState->id, blendDuration);
    }

    void AnimatorStateMachine::setMachineState(const AnimatorStateMachineState& newState)
    {
        state = newState;

        // Ensure animations for current (and previous if blending) states are loaded
        if (activeGraph)
        {
            loadAnimationForState(state.currentStateId);
            if (state.isBlending)
                loadAnimationForState(state.previousStateId);
        }
    }

    void AnimatorStateMachine::setRootMotionEnabled(bool enabled)
    {
        rootMotionEnabled = enabled;
        rootMotionFirstFrame = true;
        previousRootPosition = glm::vec3(0.0f);
        rootMotionDelta = glm::vec3(0.0f);
        rootMotionLastLoopCount = 0;
    }

    glm::vec3 AnimatorStateMachine::consumeRootMotionDelta()
    {
        glm::vec3 delta = rootMotionDelta;
        rootMotionDelta = glm::vec3(0.0f);
        return delta;
    }

    void AnimatorStateMachine::computeSocketTransforms(
        const std::vector<animator::SocketDefinition>& sockets,
        std::vector<glm::mat4>& outSocketModelTransforms) const
    {
        if (!skeletonData || currentBoneMatrices.empty() || sockets.empty())
        {
            outSocketModelTransforms.clear();
            return;
        }

        outSocketModelTransforms.resize(sockets.size());

        for (size_t i = 0; i < sockets.size(); ++i)
        {
            const auto& socket = sockets[i];
            if (socket.boneIndex >= 0 &&
                socket.boneIndex < static_cast<int32_t>(currentBoneMatrices.size()) &&
                socket.boneIndex < static_cast<int32_t>(skeletonData->bindPoses.size()))
            {
                glm::vec3 boneMeshPos = glm::vec3(
                    currentBoneMatrices[socket.boneIndex]
                    * skeletonData->bindPoses[socket.boneIndex]
                    * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

                outSocketModelTransforms[i] = glm::translate(glm::mat4(1.0f),
                    boneMeshPos + socket.localPosition);
            }
            else
            {
                outSocketModelTransforms[i] = glm::mat4(1.0f);
            }
        }
    }
}
