#include "SpotShadowCalculator.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace render::shadow
{
    SpotShadowData SpotShadowCalculator::computeSpotLightMatrices(
        const glm::vec3& lightPosition,
        const glm::vec3& lightDirection,
        float outerAngleDegrees,
        float nearPlane,
        float farPlane)
    {
        SpotShadowData result;

        result.viewMatrix = computeViewMatrix(lightPosition, lightDirection);
        result.projMatrix = computeProjection(outerAngleDegrees, nearPlane, farPlane);
        result.viewProjMatrix = result.projMatrix * result.viewMatrix;

        return result;
    }

    glm::mat4 SpotShadowCalculator::computeViewMatrix(
        const glm::vec3& lightPosition,
        const glm::vec3& lightDirection)
    {
        glm::vec3 target = lightPosition + lightDirection;
        glm::vec3 up = computeUpVector(lightDirection);
        return glm::lookAt(lightPosition, target, up);
    }

    glm::mat4 SpotShadowCalculator::computeProjection(
        float outerAngleDegrees,
        float nearPlane,
        float farPlane)
    {
        if (nearPlane <= 0.0f)
        {
            nearPlane = DEFAULT_NEAR_PLANE;
        }
        if (farPlane <= nearPlane)
        {
            farPlane = nearPlane + 1.0f;
        }

        outerAngleDegrees = glm::clamp(outerAngleDegrees, 1.0f, 89.0f);

        // FOV = outerAngle * 2 (half-angle to full cone)
        float fovDegrees = outerAngleDegrees * 2.0f;

        glm::mat4 proj = glm::perspective(
            glm::radians(fovDegrees),
            1.0f,
            nearPlane,
            farPlane
        );

        // Vulkan Y-flip
        proj[1][1] *= -1.0f;

        return proj;
    }

    glm::vec3 SpotShadowCalculator::computeUpVector(const glm::vec3& lightDirection)
    {
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

        // Use Z axis fallback when direction is nearly parallel to world up
        float dot = std::abs(glm::dot(lightDirection, worldUp));
        if (dot > 0.999f)
        {
            worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        return worldUp;
    }
}
