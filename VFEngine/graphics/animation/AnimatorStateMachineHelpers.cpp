#include "AnimatorStateMachine.hpp"

namespace animation
{
    bool AnimatorStateMachine::loadAnimationForState(uint32_t stateId)
    {
        if (!activeGraph || !animationLoadCallback)
        {
            return false;
        }

        const animator::AnimatorState* animState = activeGraph->findStateById(stateId);
        if (!animState)
        {
            loadedAnimations[stateId] = nullptr;
            return false;
        }

        if (animState->blendTree.has_value())
        {
            if (loadedBlendTreeStates.contains(stateId))
                return true;
            loadBlendTreeAnimations(*animState);
            loadedBlendTreeStates.insert(stateId);
            return true;
        }

        auto it = loadedAnimations.find(stateId);
        if (it != loadedAnimations.end())
        {
            return it->second != nullptr;
        }

        if (!animState->animationRef.isValid())
        {
            loadedAnimations[stateId] = nullptr;
            return false;
        }

        const resource::AnimationData* animData = animationLoadCallback(animState->animationRef.resolve());
        loadedAnimations[stateId] = animData;

        return animData != nullptr;
    }

    void AnimatorStateMachine::loadBlendTreeAnimations(const animator::AnimatorState& animState)
    {
        if (!animState.blendTree.has_value() || !animationLoadCallback)
            return;

        for (const auto& entry : animState.blendTree->entries)
        {
            if (entry.animationRef.isValid())
            {
                animationLoadCallback(entry.animationRef.resolve());
            }
        }
    }

    float AnimatorStateMachine::getAnimationDuration(uint32_t stateId) const
    {
        if (activeGraph)
        {
            const animator::AnimatorState* animState = activeGraph->findStateById(stateId);
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
                            return data->duration / tps;
                        }
                    }
                }
            }
        }

        auto it = loadedAnimations.find(stateId);
        if (it != loadedAnimations.end() && it->second)
        {
            float tps = it->second->ticksPerSecond > 0.0f ? it->second->ticksPerSecond : 24.0f;
            return it->second->duration / tps;
        }
        return 0.0f;
    }

    bool AnimatorStateMachine::hasBlendTree(uint32_t stateId) const
    {
        if (!activeGraph)
            return false;
        const animator::AnimatorState* animState = activeGraph->findStateById(stateId);
        return animState && animState->blendTree.has_value();
    }

    std::vector<glm::mat4> AnimatorStateMachine::evaluateBlendTreePose(
        const animator::AnimatorState& animState, float time) const
    {
        if (!animState.blendTree.has_value() || !skeletonData || !animationLoadCallback)
            return {};

        return blendTreeEvaluator.evaluate(
            animState.blendTree.value(), *parameters, *skeletonData, time, animationLoadCallback, retargetContext);
    }

    std::vector<glm::mat4> AnimatorStateMachine::evaluateBlendTreePose(
        const animator::AnimatorState& animState, float time, glm::vec3& outRootPosition) const
    {
        if (!animState.blendTree.has_value() || !skeletonData || !animationLoadCallback)
            return {};

        return blendTreeEvaluator.evaluate(
            animState.blendTree.value(), *parameters, *skeletonData, time, animationLoadCallback, outRootPosition, retargetContext);
    }
}
