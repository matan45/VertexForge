#include "HandIKHelper.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace animator::ik
{
    HandIKResult HandIKHelper::calculateHandTarget(
        const glm::vec3& handWorldPosition,
        const glm::vec3& shoulderWorldPosition,
        const glm::vec3& targetWorldPosition,
        const std::optional<glm::quat>& targetRotation,
        const HandIKConfig& config)
    {
        HandIKResult result;

        glm::vec3 shoulderToTarget = targetWorldPosition - shoulderWorldPosition;
        float reachDistance = glm::length(shoulderToTarget);

        if (reachDistance < 0.001f)
        {
            result.targetPosition = handWorldPosition;
            result.weight = 0.0f;
            result.isReachable = false;
            return result;
        }

        // Check if target is within reach
        if (reachDistance > config.maxReachDistance)
        {
            // Clamp to max reach - extend toward target but at max distance
            glm::vec3 direction = shoulderToTarget / reachDistance;
            result.targetPosition = shoulderWorldPosition + direction * config.maxReachDistance;
            result.isReachable = false;

            // Weight falls off beyond max reach
            float overreach = reachDistance / config.maxReachDistance;
            result.weight = glm::clamp(2.0f - overreach, 0.0f, 1.0f);
        }
        else
        {
            result.targetPosition = targetWorldPosition;
            result.isReachable = true;
            result.weight = 1.0f;
        }

        // Apply target rotation with blend
        if (targetRotation.has_value() && config.gripRotationBlend > 0.0f)
        {
            result.targetRotation = *targetRotation;
        }

        return result;
    }

    HandIKResult HandIKHelper::calculateTwoHandedGrip(
        const glm::vec3& dominantHandWorldPosition,
        const glm::quat& dominantHandWorldRotation,
        const glm::vec3& gripOffset,
        const glm::vec3& offHandShoulderWorldPosition,
        const HandIKConfig& config)
    {
        // Compute off-hand grip position relative to dominant hand
        glm::vec3 worldGripOffset = dominantHandWorldRotation * gripOffset;
        glm::vec3 gripPosition = dominantHandWorldPosition + worldGripOffset;

        // Use the standard hand target calculation with the computed grip point
        return calculateHandTarget(
            gripPosition,
            offHandShoulderWorldPosition,
            gripPosition,
            dominantHandWorldRotation,
            config);
    }
}
