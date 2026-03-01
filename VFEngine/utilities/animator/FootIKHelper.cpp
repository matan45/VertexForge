#include "FootIKHelper.hpp"
#include <algorithm>
#include <cmath>

namespace animator::ik
{
    FootIKResult FootIKHelper::calculateFootTarget(
        const glm::vec3& footWorldPosition,
        const glm::vec3& characterUp,
        const FootRaycastResult& raycastResult,
        const FootIKConfig& config)
    {
        FootIKResult result;

        if (!raycastResult.hit)
        {
            result.targetPosition = footWorldPosition;
            result.weight = 0.0f;
            result.isGrounded = false;
            return result;
        }

        glm::vec3 groundTarget = raycastResult.hitPoint + characterUp * config.footHeight;

        float heightDifference = std::abs(groundTarget.y - footWorldPosition.y);
        if (heightDifference > config.maxStepHeight)
        {
            result.targetPosition = footWorldPosition;
            result.weight = 0.0f;
            result.isGrounded = false;
            return result;
        }

        result.targetPosition = groundTarget;
        result.isGrounded = true;

        // Weight falls off as we approach the max step height
        float normalizedDist = heightDifference / config.maxStepHeight;
        result.weight = 1.0f - (normalizedDist * normalizedDist);

        glm::vec3 up = glm::normalize(characterUp);
        glm::vec3 normal = glm::normalize(raycastResult.hitNormal);

        float dot = glm::dot(up, normal);
        if (dot < 0.999f)
        {
            glm::vec3 axis = glm::cross(up, normal);
            float axisLen = glm::length(axis);
            if (axisLen > 0.0001f)
            {
                axis /= axisLen;
                float angle = std::acos(glm::clamp(dot, -1.0f, 1.0f));
                result.targetRotation = glm::angleAxis(angle, axis);
            }
        }

        return result;
    }

    float FootIKHelper::calculatePelvisOffset(
        const FootIKResult& leftFoot,
        const FootIKResult& rightFoot,
        float currentOffset,
        float deltaTime,
        float adjustSpeed)
    {
        // Target offset is the lowest foot displacement (most negative Y delta)
        float targetOffset = 0.0f;

        if (leftFoot.isGrounded && rightFoot.isGrounded)
        {
            float leftDelta = leftFoot.targetPosition.y;
            float rightDelta = rightFoot.targetPosition.y;
            targetOffset = std::min(leftDelta, rightDelta) -
                           std::max(leftDelta, rightDelta);
            targetOffset = std::min(targetOffset, 0.0f);
        }
        else if (!leftFoot.isGrounded && !rightFoot.isGrounded)
        {
            targetOffset = 0.0f;
        }

        float t = 1.0f - std::exp(-adjustSpeed * deltaTime);
        return glm::mix(currentOffset, targetOffset, t);
    }
}
