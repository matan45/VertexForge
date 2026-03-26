#pragma once

#include "types/RenderSettings.hpp"
#include "ShadowTypes.hpp"
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

    struct ZBoundsResult
    {
        float minZ = 0.0f;
        float maxZ = 0.0f;
        float nearClip = 0.0f;
        float farClip = 0.0f;
    };

    struct SubFrustumParams
    {
        std::array<glm::vec3, 8> worldCorners;
        float tNear = 0.0f;
        float tFar = 1.0f;
    };

    struct FrustumSphere
    {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
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
        static float quantizeRadius(float radius);
        static ZBoundsResult computeZBounds(
            const glm::mat4& viewMatrix,
            const std::array<glm::vec3, 8>& frustumCorners,
            float radius);
        static std::array<glm::vec3, 8> interpolateSubFrustum(
            const SubFrustumParams& params);
        static FrustumSphere computeFrustumBoundingSphere(
            const std::array<glm::vec3, 8>& frustumCorners);
        static glm::vec3 snapCenterToLightGrid(
            const glm::vec3& frustumCenter,
            const LightSpaceAxes& axes,
            float texelSize);
    };
}
