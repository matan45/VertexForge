#include "AnimationLOD.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace animation
{
    AnimationLODLevel AnimationLODManager::computeLODLevel(float distanceSquared) const
    {
        float dist2_0 = config.distanceThresholds[0] * config.distanceThresholds[0];
        float dist2_1 = config.distanceThresholds[1] * config.distanceThresholds[1];
        float dist2_2 = config.distanceThresholds[2] * config.distanceThresholds[2];

        if (distanceSquared < dist2_0)
            return AnimationLODLevel::LOD0;
        if (distanceSquared < dist2_1)
            return AnimationLODLevel::LOD1;
        if (distanceSquared < dist2_2)
            return AnimationLODLevel::LOD2;
        return AnimationLODLevel::LOD3;
    }

    bool AnimationLODManager::shouldEvaluateThisFrame(const EntityAnimationLODState& state) const
    {
        uint8_t lodIdx = static_cast<uint8_t>(state.currentLOD);
        uint32_t interval = config.updateIntervals[lodIdx];

        // interval 0 means frozen (LOD3)
        if (interval == 0)
            return false;

        return state.framesSinceLastEval >= interval;
    }

    void AnimationLODManager::updateEntityLOD(EntityAnimationLODState& state, float distanceSquared, float deltaTime)
    {
        state.distanceSquared = distanceSquared;
        AnimationLODLevel newLOD = computeLODLevel(distanceSquared);

        if (newLOD != state.currentLOD)
        {
            state.previousLOD = state.currentLOD;
            state.currentLOD = newLOD;
            state.inTransition = true;
            state.lodTransitionAlpha = 0.0f;
        }

        if (state.inTransition)
        {
            // Smooth transition over configurable frame count
            float transitionSpeed = config.transitionBlendFrames > 0.0f
                ? 1.0f / config.transitionBlendFrames
                : 1.0f;
            state.lodTransitionAlpha += transitionSpeed;
            if (state.lodTransitionAlpha >= 1.0f)
            {
                state.lodTransitionAlpha = 1.0f;
                state.inTransition = false;
            }
        }

        state.framesSinceLastEval++;
    }

    void AnimationLODManager::interpolateCachedPoses(const EntityAnimationLODState& state,
                                                      float t,
                                                      std::vector<glm::mat4>& outMatrices) const
    {
        if (!state.hasCachedPoseA || !state.hasCachedPoseB)
            return;

        const auto& poseA = state.cachedPoseA;
        const auto& poseB = state.cachedPoseB;
        size_t boneCount = std::min(poseA.size(), poseB.size());
        outMatrices.resize(boneCount);

        t = std::clamp(t, 0.0f, 1.0f);

        for (size_t i = 0; i < boneCount; ++i)
        {
            // Simple matrix lerp for bone matrices (sufficient for interpolation between close poses)
            outMatrices[i] = poseA[i] * (1.0f - t) + poseB[i] * t;
        }
    }

    void AnimationLODManager::cachePose(EntityAnimationLODState& state, const std::vector<glm::mat4>& pose)
    {
        state.cachedPoseA = state.cachedPoseB;
        state.hasCachedPoseA = state.hasCachedPoseB;

        state.cachedPoseB = pose;
        state.hasCachedPoseB = true;
    }
}
