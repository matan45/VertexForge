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
        // Compute target point (position + direction)
        glm::vec3 target = lightPosition + lightDirection;

        // Get appropriate up vector
        glm::vec3 up = computeUpVector(lightDirection);

        return glm::lookAt(lightPosition, target, up);
    }

    glm::mat4 SpotShadowCalculator::computeProjection(
        float outerAngleDegrees,
        float nearPlane,
        float farPlane)
    {
        // Ensure valid near/far planes
        if (nearPlane <= 0.0f)
        {
            nearPlane = DEFAULT_NEAR_PLANE;
        }
        if (farPlane <= nearPlane)
        {
            farPlane = nearPlane + 1.0f;
        }

        // Clamp outer angle to reasonable range
        if (outerAngleDegrees <= 0.0f)
        {
            outerAngleDegrees = 45.0f;
        }
        if (outerAngleDegrees > 89.0f)
        {
            outerAngleDegrees = 89.0f;
        }

        // FOV = outerAngle * 2 to cover the full cone
        // outerAngle is the half-angle from center to edge
        float fovDegrees = outerAngleDegrees * 2.0f;

        // Create perspective projection
        // 1:1 aspect ratio for square shadow map tiles
        glm::mat4 proj = glm::perspective(
            glm::radians(fovDegrees),
            1.0f,  // aspect ratio = 1.0 for square shadow map
            nearPlane,
            farPlane
        );

        // Apply Vulkan Y-flip (Vulkan has Y pointing down in NDC)
        proj[1][1] *= -1.0f;

        return proj;
    }

    glm::vec3 SpotShadowCalculator::computeUpVector(const glm::vec3& lightDirection)
    {
        // Default world up vector
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

        // Check if direction is nearly parallel to world up
        // (dot product close to 1 or -1)
        float dot = std::abs(glm::dot(lightDirection, worldUp));
        if (dot > 0.999f)
        {
            // Use Z axis as fallback when looking straight up or down
            worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        return worldUp;
    }
}
