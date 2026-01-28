#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::shadow
{
    struct SpotShadowData
    {
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projMatrix{1.0f};
        glm::mat4 viewProjMatrix{1.0f};
    };

    // Computes spot light shadow matrices (perspective projection covering full cone)
    class SpotShadowCalculator
    {
    public:
        static constexpr float DEFAULT_NEAR_PLANE = 0.1f;

        static SpotShadowData computeSpotLightMatrices(
            const glm::vec3& lightPosition,
            const glm::vec3& lightDirection,
            float outerAngleDegrees,
            float nearPlane,
            float farPlane);

    private:
        static glm::mat4 computeViewMatrix(
            const glm::vec3& lightPosition,
            const glm::vec3& lightDirection);

        static glm::mat4 computeProjection(
            float outerAngleDegrees,
            float nearPlane,
            float farPlane);

        static glm::vec3 computeUpVector(const glm::vec3& lightDirection);
    };
}
