#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::shadow
{
    /**
     * Data structure containing computed spot light shadow parameters.
     */
    struct SpotShadowData
    {
        glm::mat4 viewMatrix{1.0f};     // Light view matrix
        glm::mat4 projMatrix{1.0f};     // Perspective projection matrix
        glm::mat4 viewProjMatrix{1.0f}; // Combined view-projection matrix
    };

    /**
     * SpotShadowCalculator - Utility class for computing spot light shadow matrices.
     *
     * Provides static methods for:
     * - Computing view matrix from light position and direction
     * - Computing perspective projection matrix (FOV = outerAngle * 2)
     * - Applying Vulkan Y-flip to projection matrix
     *
     * Spot lights use a single 2D shadow map with perspective projection
     * that covers the full cone angle.
     */
    class SpotShadowCalculator
    {
    public:
        static constexpr float DEFAULT_NEAR_PLANE = 0.1f;

        /**
         * Compute view-projection matrices for a spot light.
         *
         * @param lightPosition World-space position of the spot light
         * @param lightDirection Normalized world-space direction the light is pointing
         * @param outerAngleDegrees Outer cone angle in degrees (half-angle from center)
         * @param nearPlane Near plane distance (typically small, e.g., 0.1)
         * @param farPlane Far plane distance (typically light range)
         * @return SpotShadowData structure with computed matrices
         */
        static SpotShadowData computeSpotLightMatrices(
            const glm::vec3& lightPosition,
            const glm::vec3& lightDirection,
            float outerAngleDegrees,
            float nearPlane,
            float farPlane);

        /**
         * Compute view matrix for spot light.
         *
         * @param lightPosition World-space position of the spot light
         * @param lightDirection Normalized world-space direction the light is pointing
         * @return View matrix looking from light position toward direction
         */
        static glm::mat4 computeViewMatrix(
            const glm::vec3& lightPosition,
            const glm::vec3& lightDirection);

        /**
         * Compute perspective projection matrix for spot light shadow.
         * Uses FOV = outerAngle * 2 to cover the full cone, 1:1 aspect ratio,
         * and applies Vulkan Y-flip.
         *
         * @param outerAngleDegrees Outer cone angle in degrees (half-angle)
         * @param nearPlane Near plane distance
         * @param farPlane Far plane distance
         * @return Vulkan-compatible perspective projection matrix
         */
        static glm::mat4 computeProjection(
            float outerAngleDegrees,
            float nearPlane,
            float farPlane);

    private:
        /**
         * Compute appropriate up vector for view matrix.
         * Handles edge case when direction is parallel to world up.
         *
         * @param lightDirection Normalized light direction
         * @return Up vector suitable for lookAt computation
         */
        static glm::vec3 computeUpVector(const glm::vec3& lightDirection);
    };
}
