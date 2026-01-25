#include "PointShadowCalculator.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace render::shadow
{
    // Cube face directions: +X, -X, +Y, -Y, +Z, -Z
    // These define which direction each cube face looks
    const std::array<glm::vec3, PointShadowCalculator::FACE_COUNT>
        PointShadowCalculator::s_faceDirections = {{
            { 1.0f,  0.0f,  0.0f},  // Face 0: +X (right)
            {-1.0f,  0.0f,  0.0f},  // Face 1: -X (left)
            { 0.0f,  1.0f,  0.0f},  // Face 2: +Y (up)
            { 0.0f, -1.0f,  0.0f},  // Face 3: -Y (down)
            { 0.0f,  0.0f,  1.0f},  // Face 4: +Z (front)
            { 0.0f,  0.0f, -1.0f}   // Face 5: -Z (back)
        }};

    // Up vectors for each face (matching IBLTypes.hpp captureViews)
    // These ensure consistent orientation for each cube face
    const std::array<glm::vec3, PointShadowCalculator::FACE_COUNT>
        PointShadowCalculator::s_faceUpVectors = {{
            { 0.0f, -1.0f,  0.0f},  // Face 0: +X, up = -Y
            { 0.0f, -1.0f,  0.0f},  // Face 1: -X, up = -Y
            { 0.0f,  0.0f,  1.0f},  // Face 2: +Y, up = +Z
            { 0.0f,  0.0f, -1.0f},  // Face 3: -Y, up = -Z
            { 0.0f, -1.0f,  0.0f},  // Face 4: +Z, up = -Y
            { 0.0f, -1.0f,  0.0f}   // Face 5: -Z, up = -Y
        }};

    std::array<PointShadowFaceData, PointShadowCalculator::FACE_COUNT>
        PointShadowCalculator::computeCubeFaceMatrices(
            const glm::vec3& lightPosition,
            float nearPlane,
            float farPlane)
    {
        std::array<PointShadowFaceData, FACE_COUNT> result;

        // Compute shared projection matrix (same for all 6 faces)
        glm::mat4 projection = computeCubeProjection(nearPlane, farPlane);

        // Compute view and combined matrices for each face
        for (uint32_t face = 0; face < FACE_COUNT; ++face)
        {
            result[face].faceIndex = face;
            result[face].viewMatrix = computeFaceViewMatrix(lightPosition, face);
            result[face].projMatrix = projection;
            result[face].viewProjMatrix = projection * result[face].viewMatrix;
        }

        return result;
    }

    glm::mat4 PointShadowCalculator::computeFaceViewMatrix(
        const glm::vec3& lightPosition,
        uint32_t faceIndex)
    {
        if (faceIndex >= FACE_COUNT)
        {
            return glm::mat4(1.0f);
        }

        // Look from light position toward the face direction
        glm::vec3 target = lightPosition + s_faceDirections[faceIndex];
        glm::vec3 up = s_faceUpVectors[faceIndex];

        return glm::lookAt(lightPosition, target, up);
    }

    glm::mat4 PointShadowCalculator::computeCubeProjection(float nearPlane, float farPlane)
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

        // 90 degree FOV, 1:1 aspect ratio (square cube faces)
        glm::mat4 proj = glm::perspective(
            glm::radians(FOV_DEGREES),
            1.0f,  // aspect ratio = 1.0 for square cube faces
            nearPlane,
            farPlane
        );

        // Apply Vulkan Y-flip (Vulkan has Y pointing down in NDC)
        proj[1][1] *= -1.0f;

        return proj;
    }
}
