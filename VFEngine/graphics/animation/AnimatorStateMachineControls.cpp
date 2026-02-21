#include "AnimatorStateMachine.hpp"
#include <glm/gtc/matrix_transform.hpp>

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

        state = AnimatorStateMachineState{};
        state.currentStateId = animatorData->graph.defaultStateId;
        state.isPlaying = true;

        parameters.initializeFromGraph(animatorData->graph);

        loadAnimationForState(state.currentStateId);
    }

    void AnimatorStateMachine::forceTransitionTo(uint32_t stateId, float blendDuration)
    {
        if (!animatorData)
            return;

        const animator::AnimatorState* targetState = animatorData->graph.findStateById(stateId);
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
        if (!animatorData)
            return;

        const animator::AnimatorState* targetState = animatorData->graph.findStateByName(stateName);
        if (!targetState)
        {
            return;
        }

        forceTransitionTo(targetState->id, blendDuration);
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
