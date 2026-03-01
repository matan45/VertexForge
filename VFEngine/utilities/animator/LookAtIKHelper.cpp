#include "LookAtIKHelper.hpp"
#include <algorithm>
#include <cmath>

namespace animator::ik
{
    glm::vec3 LookAtIKHelper::calculateLookAtTarget(
        const glm::vec3& headWorldPosition,
        const glm::vec3& headForwardDirection,
        const glm::vec3& targetWorldPosition,
        const LookAtConfig& config,
        float* outWeight)
    {
        glm::vec3 toTarget = targetWorldPosition - headWorldPosition;
        float distance = glm::length(toTarget);

        if (distance < 0.001f)
        {
            if (outWeight) *outWeight = 0.0f;
            return headWorldPosition + headForwardDirection;
        }

        glm::vec3 dirToTarget = toTarget / distance;
        glm::vec3 forward = glm::normalize(headForwardDirection);

        // Calculate angle between forward and target direction
        float dot = glm::clamp(glm::dot(forward, dirToTarget), -1.0f, 1.0f);
        float angle = std::acos(dot);

        // Dead zone - no IK needed
        if (angle < config.deadZoneAngle)
        {
            if (outWeight) *outWeight = 0.0f;
            return targetWorldPosition;
        }

        // Calculate weight ramp from dead zone to full angle
        float effectiveRange = config.maxAngle - config.deadZoneAngle;
        float effectiveAngle = angle - config.deadZoneAngle;
        float weight = (effectiveRange > 0.0f)
                            ? glm::clamp(effectiveAngle / effectiveRange, 0.0f, 1.0f)
                            : 1.0f;

        if (outWeight) *outWeight = weight;

        // Clamp direction if beyond max angle
        if (angle > config.maxAngle)
        {
            // Rotate forward toward target by maxAngle
            glm::vec3 axis = glm::cross(forward, dirToTarget);
            float axisLen = glm::length(axis);
            if (axisLen > 0.0001f)
            {
                axis /= axisLen;
                glm::quat rotation = glm::angleAxis(config.maxAngle, axis);
                glm::vec3 clampedDir = rotation * forward;
                return headWorldPosition + clampedDir * distance;
            }
        }

        return targetWorldPosition;
    }

    glm::vec3 LookAtIKHelper::smoothTarget(
        const glm::vec3& currentTarget,
        const glm::vec3& desiredTarget,
        float deltaTime,
        float blendSpeed)
    {
        float t = 1.0f - std::exp(-blendSpeed * deltaTime);
        return glm::mix(currentTarget, desiredTarget, t);
    }
}
