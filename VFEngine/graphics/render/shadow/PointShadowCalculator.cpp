#include "PointShadowCalculator.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace render::shadow
{
    const std::array<glm::vec3, ShadowConstants::CUBE_FACE_COUNT>
        PointShadowCalculator::faceDirections = {{
            { 1.0f,  0.0f,  0.0f},
            {-1.0f,  0.0f,  0.0f},
            { 0.0f,  1.0f,  0.0f},
            { 0.0f, -1.0f,  0.0f},
            { 0.0f,  0.0f,  1.0f},
            { 0.0f,  0.0f, -1.0f}
        }};

    const std::array<glm::vec3, ShadowConstants::CUBE_FACE_COUNT>
        PointShadowCalculator::faceUpVectors = {{
            { 0.0f, -1.0f,  0.0f},
            { 0.0f, -1.0f,  0.0f},
            { 0.0f,  0.0f,  1.0f},
            { 0.0f,  0.0f, -1.0f},
            { 0.0f, -1.0f,  0.0f},
            { 0.0f, -1.0f,  0.0f}
        }};

    std::array<PointShadowFaceData, ShadowConstants::CUBE_FACE_COUNT>
        PointShadowCalculator::computeCubeFaceMatrices(
            const glm::vec3& lightPosition,
            float nearPlane,
            float farPlane)
    {
        std::array<PointShadowFaceData, ShadowConstants::CUBE_FACE_COUNT> result;
        glm::mat4 projection = computeCubeProjection(nearPlane, farPlane);

        for (uint32_t face = 0; face < ShadowConstants::CUBE_FACE_COUNT; ++face)
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
        if (faceIndex >= ShadowConstants::CUBE_FACE_COUNT)
            return glm::mat4(1.0f);

        glm::vec3 target = lightPosition + faceDirections[faceIndex];
        glm::vec3 up = faceUpVectors[faceIndex];

        return glm::lookAt(lightPosition, target, up);
    }

    glm::mat4 PointShadowCalculator::computeCubeProjection(float nearPlane, float farPlane)
    {
        if (nearPlane <= 0.0f)
            nearPlane = DEFAULT_NEAR_PLANE;
        if (farPlane <= nearPlane)
            farPlane = nearPlane + 1.0f;

        glm::mat4 proj = glm::perspective(
            glm::radians(FOV_DEGREES),
            1.0f,
            nearPlane,
            farPlane
        );

        proj[1][1] *= -1.0f;

        return proj;
    }
}
