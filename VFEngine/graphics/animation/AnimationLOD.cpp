#include "AnimationLOD.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
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

        state.currentLOD = newLOD;

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
        size_t minCount = std::min(poseA.size(), poseB.size());
        size_t maxCount = std::max(poseA.size(), poseB.size());
        outMatrices.resize(maxCount, glm::mat4(1.0f));

        t = std::clamp(t, 0.0f, 1.0f);

        for (size_t i = 0; i < minCount; ++i)
        {
            // Decompose both matrices into T/R/S, SLERP rotation, lerp the rest
            glm::vec3 scaleA, transA, skewA;
            glm::vec4 perspA;
            glm::quat rotA;
            glm::decompose(poseA[i], scaleA, rotA, transA, skewA, perspA);

            glm::vec3 scaleB, transB, skewB;
            glm::vec4 perspB;
            glm::quat rotB;
            glm::decompose(poseB[i], scaleB, rotB, transB, skewB, perspB);

            glm::vec3 interpTrans = glm::mix(transA, transB, t);
            glm::quat interpRot = glm::slerp(rotA, rotB, t);
            glm::vec3 interpScale = glm::mix(scaleA, scaleB, t);

            glm::mat4 T = glm::translate(glm::mat4(1.0f), interpTrans);
            glm::mat4 R = glm::mat4_cast(interpRot);
            glm::mat4 S = glm::scale(glm::mat4(1.0f), interpScale);
            outMatrices[i] = T * R * S;
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
