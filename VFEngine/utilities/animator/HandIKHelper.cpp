#include "HandIKHelper.hpp"

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

        if (reachDistance > config.maxReachDistance)
        {
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

        if (targetRotation.has_value() && config.gripRotationBlend > 0.0f)
        {
            float blend = glm::clamp(config.gripRotationBlend, 0.0f, 1.0f);
            glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
            result.targetRotation = glm::slerp(identity, *targetRotation, blend);
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
        glm::vec3 worldGripOffset = dominantHandWorldRotation * gripOffset;
        glm::vec3 gripPosition = dominantHandWorldPosition + worldGripOffset;

        return calculateHandTarget(
            gripPosition,
            offHandShoulderWorldPosition,
            gripPosition,
            dominantHandWorldRotation,
            config);
    }
}
