#pragma once

#include "types/RenderSettings.hpp"
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstdint>

namespace render::shadow
{
    struct CascadeData
    {
        float nearDistance = 0.0f;
        float farDistance = 0.0f;
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projMatrix{1.0f};
        glm::mat4 viewProjMatrix{1.0f};
        float texelSize = 0.0f;
    };

    class CascadeShadowCalculator
    {
    public:
        static std::vector<float> computeSplitDistances(
            float cameraNear,
            float cameraFar,
            uint32_t cascadeCount,
            types::CascadeSplitMode splitMode,
            float lambda = 0.75f);

        static std::array<glm::vec3, 8> getFrustumCornersWorldSpace(
            const glm::mat4& cameraView,
            const glm::mat4& cameraProjection,
            float nearDist,
            float farDist);

        static CascadeData computeCascadeMatrix(
            const std::array<glm::vec3, 8>& frustumCorners,
            const glm::vec3& lightDirection,
            uint32_t shadowMapResolution);

    private:
        static float snapToTexel(float value, float texelSize);
    };
}
