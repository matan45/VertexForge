#pragma once

#include "ShadowTypes.hpp"
#include <glm/glm.hpp>
#include <array>

namespace render::shadow
{
    struct PointShadowFaceData
    {
        uint32_t faceIndex = 0;
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projMatrix{1.0f};
        glm::mat4 viewProjMatrix{1.0f};
    };

    class PointShadowCalculator
    {
    public:
        static constexpr float FOV_DEGREES = 90.0f;
        static constexpr float DEFAULT_NEAR_PLANE = 0.1f;

        static std::array<PointShadowFaceData, ShadowConstants::CUBE_FACE_COUNT> computeCubeFaceMatrices(
            const glm::vec3& lightPosition,
            float nearPlane,
            float farPlane);

    private:
        static const std::array<glm::vec3, ShadowConstants::CUBE_FACE_COUNT> faceDirections;
        static const std::array<glm::vec3, ShadowConstants::CUBE_FACE_COUNT> faceUpVectors;

        static glm::mat4 computeFaceViewMatrix(const glm::vec3& lightPosition, uint32_t faceIndex);
        static glm::mat4 computeCubeProjection(float nearPlane, float farPlane);
    };
}
