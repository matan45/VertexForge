#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>

namespace render::shadow
{
    /**
     * Data structure containing computed point light shadow parameters for one cube face.
     */
    struct PointShadowFaceData
    {
        uint32_t faceIndex = 0;         // 0-5: +X, -X, +Y, -Y, +Z, -Z
        glm::mat4 viewMatrix{1.0f};     // Light view matrix for this face
        glm::mat4 projMatrix{1.0f};     // Perspective projection matrix (90 FOV)
        glm::mat4 viewProjMatrix{1.0f}; // Combined view-projection matrix
    };

    /**
     * PointShadowCalculator - Utility class for computing point light cube shadow matrices.
     *
     * Provides static methods for:
     * - Computing 6 cube face view matrices from light world position
     * - Computing perspective projection matrix (90 FOV, 1:1 aspect)
     * - Applying Vulkan Y-flip to projection matrix
     *
     * Cube face ordering follows Vulkan/OpenGL convention:
     * 0: +X (right), 1: -X (left), 2: +Y (up), 3: -Y (down), 4: +Z (front), 5: -Z (back)
     */
    class PointShadowCalculator
    {
    public:
        static constexpr uint32_t FACE_COUNT = 6;
        static constexpr float FOV_DEGREES = 90.0f;
        static constexpr float DEFAULT_NEAR_PLANE = 0.1f;

        /**
         * Compute all 6 cube face view-projection matrices for a point light.
         *
         * @param lightPosition World-space position of the point light
         * @param nearPlane Near plane distance (typically small, e.g., 0.1)
         * @param farPlane Far plane distance (typically light radius)
         * @return Array of 6 PointShadowFaceData structures
         */
        static std::array<PointShadowFaceData, FACE_COUNT> computeCubeFaceMatrices(
            const glm::vec3& lightPosition,
            float nearPlane,
            float farPlane);

        /**
         * Compute view matrix for a single cube face.
         *
         * @param lightPosition World-space position of the point light
         * @param faceIndex Cube face index (0-5)
         * @return View matrix looking from light position toward face direction
         */
        static glm::mat4 computeFaceViewMatrix(
            const glm::vec3& lightPosition,
            uint32_t faceIndex);

        /**
         * Compute perspective projection matrix for cube shadow rendering.
         * Uses 90 degree FOV, 1:1 aspect ratio, and applies Vulkan Y-flip.
         *
         * @param nearPlane Near plane distance
         * @param farPlane Far plane distance
         * @return Vulkan-compatible perspective projection matrix
         */
        static glm::mat4 computeCubeProjection(float nearPlane, float farPlane);

    private:
        // Cube face view directions and up vectors
        // Vulkan cubemap face order: +X, -X, +Y, -Y, +Z, -Z
        static const std::array<glm::vec3, FACE_COUNT> s_faceDirections;
        static const std::array<glm::vec3, FACE_COUNT> s_faceUpVectors;
    };
}
