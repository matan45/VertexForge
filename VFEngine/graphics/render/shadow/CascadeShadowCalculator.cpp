#include "CascadeShadowCalculator.hpp"
#include "ShadowTypes.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace render::shadow
{
    std::vector<float> CascadeShadowCalculator::computeSplitDistances(
        float cameraNear,
        float cameraFar,
        uint32_t cascadeCount,
        types::CascadeSplitMode splitMode,
        float lambda)
    {
        cascadeCount = std::clamp(cascadeCount, 1u, 4u);

        std::vector<float> splits(cascadeCount + 1);
        splits[0] = cameraNear;
        splits[cascadeCount] = cameraFar;

        if (cameraNear <= 0.0f)
            cameraNear = 0.01f;

        float range = cameraFar - cameraNear;
        float ratio = cameraFar / cameraNear;

        for (uint32_t i = 1; i < cascadeCount; ++i)
        {
            float p = static_cast<float>(i) / static_cast<float>(cascadeCount);

            switch (splitMode)
            {
                case types::CascadeSplitMode::Linear:
                    splits[i] = cameraNear + range * p;
                    break;

                case types::CascadeSplitMode::Logarithmic:
                    // C_log(i) = near * (far/near)^(i/n)
                    splits[i] = cameraNear * std::exp(std::log(ratio) * p);
                    break;

                case types::CascadeSplitMode::Practical:
                default:
                    // GPU Gems 3: lambda * log + (1-lambda) * linear
                    {
                        float linearSplit = cameraNear + range * p;
                        float logSplit = cameraNear * std::exp(std::log(ratio) * p);
                        splits[i] = lambda * logSplit + (1.0f - lambda) * linearSplit;
                    }
                    break;
            }
        }

        return splits;
    }

    std::array<glm::vec3, 8> CascadeShadowCalculator::interpolateSubFrustum(
        const SubFrustumParams& params)
    {
        std::array<glm::vec3, 8> subFrustumCorners;
        for (int i = 0; i < 4; ++i)
        {
            glm::vec3 nearCorner = params.worldCorners[i];
            glm::vec3 farCorner = params.worldCorners[i + 4];

            subFrustumCorners[i] = glm::mix(nearCorner, farCorner, params.tNear);
            subFrustumCorners[i + 4] = glm::mix(nearCorner, farCorner, params.tFar);
        }
        return subFrustumCorners;
    }

    std::array<glm::vec3, 8> CascadeShadowCalculator::getFrustumCornersWorldSpace(
        const glm::mat4& cameraView,
        const glm::mat4& cameraProjection,
        float nearDist,
        float farDist)
    {
        glm::mat4 invViewProj = glm::inverse(cameraProjection * cameraView);

        // Vulkan NDC: z=0 near, z=1 far
        constexpr std::array<glm::vec4, 8> ndcCorners = {{
            {-1.0f, -1.0f, 0.0f, 1.0f},
            { 1.0f, -1.0f, 0.0f, 1.0f},
            { 1.0f,  1.0f, 0.0f, 1.0f},
            {-1.0f,  1.0f, 0.0f, 1.0f},
            {-1.0f, -1.0f, 1.0f, 1.0f},
            { 1.0f, -1.0f, 1.0f, 1.0f},
            { 1.0f,  1.0f, 1.0f, 1.0f},
            {-1.0f,  1.0f, 1.0f, 1.0f},
        }};

        std::array<glm::vec3, 8> worldCorners;
        for (size_t i = 0; i < 8; ++i)
        {
            glm::vec4 worldPos = invViewProj * ndcCorners[i];
            worldCorners[i] = glm::vec3(worldPos) / worldPos.w;
        }

        // Extract camera position from view matrix: V = [R | -R*eye]
        glm::vec3 cameraPos = -glm::vec3(cameraView[3]) * glm::mat3(cameraView);

        glm::vec3 nearCenter = (worldCorners[0] + worldCorners[1] + worldCorners[2] + worldCorners[3]) * 0.25f;
        glm::vec3 farCenter = (worldCorners[4] + worldCorners[5] + worldCorners[6] + worldCorners[7]) * 0.25f;

        float fullNear = glm::length(nearCenter - cameraPos);
        float fullFar = glm::length(farCenter - cameraPos);
        float fullRange = fullFar - fullNear;

        float tNear = (fullRange > 0.0001f) ? (nearDist - fullNear) / fullRange : 0.0f;
        float tFar = (fullRange > 0.0001f) ? (farDist - fullNear) / fullRange : 1.0f;

        tNear = std::clamp(tNear, 0.0f, 1.0f);
        tFar = std::clamp(tFar, 0.0f, 1.0f);

        return interpolateSubFrustum({worldCorners, tNear, tFar});
    }

    float CascadeShadowCalculator::quantizeRadius(float radius)
    {
        // Quantize radius to LARGE steps for temporal stability
        // Using power-of-2 buckets ensures radius only changes when cascade size roughly doubles
        // This prevents "breathing" from small floating-point variations
        if (radius > 0.0f)
        {
            float log2Radius = std::log2(radius);
            // Round up to nearest 0.5 in log space (i.e., sqrt(2) multiplier buckets)
            // This gives ~41% size increments, which is coarse enough to be stable
            float quantizedLog = std::ceil(log2Radius * 2.0f) / 2.0f;
            return std::pow(2.0f, quantizedLog);
        }
        return 1.0f;  // Fallback for degenerate cases
    }

    ZBoundsResult CascadeShadowCalculator::computeZBounds(
        const glm::mat4& viewMatrix,
        const std::array<glm::vec3, 8>& frustumCorners,
        float radius)
    {
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();
        for (const auto& corner : frustumCorners)
        {
            glm::vec3 lightSpaceCorner = glm::vec3(viewMatrix * glm::vec4(corner, 1.0f));
            minZ = std::min(minZ, lightSpaceCorner.z);
            maxZ = std::max(maxZ, lightSpaceCorner.z);
        }

        // Stabilize Z range to prevent depth precision shifts during movement
        float zRange = maxZ - minZ;
        // Extend Z far behind the camera frustum to capture shadow casters that
        // are between the light source and the visible scene. 3x the frustum depth
        // and a minimum of 800 units prevents shadows from disappearing when the
        // camera views objects from the opposite side of the light direction.
        float zExtension = std::max(zRange * 3.0f, 800.0f);

        // Scale Z quantization with cascade radius -- near cascades use finer steps
        // to prevent visible shadow popping, far cascades use coarser steps
        float zQuantization = std::max(1.0f, std::min(10.0f, radius * 0.05f));
        minZ = std::floor(minZ / zQuantization) * zQuantization;
        maxZ = std::ceil(maxZ / zQuantization) * zQuantization;

        // orthoRH_ZO expects positive near/far distances
        // In RH light space: more negative Z = farther from light
        float nearClip = -maxZ;
        float farClip = -minZ + zExtension;

        // Quantize near/far as well for additional stability
        nearClip = std::max(0.1f, std::floor(nearClip / zQuantization) * zQuantization);
        farClip = std::ceil(farClip / zQuantization) * zQuantization;

        if (farClip <= nearClip) farClip = nearClip + zQuantization;

        return {minZ, maxZ, nearClip, farClip};
    }

    FrustumSphere CascadeShadowCalculator::computeFrustumBoundingSphere(
        const std::array<glm::vec3, 8>& frustumCorners)
    {
        FrustumSphere sphere;
        for (const auto& corner : frustumCorners)
            sphere.center += corner;
        sphere.center /= 8.0f;

        for (const auto& corner : frustumCorners)
        {
            float dist = glm::length(corner - sphere.center);
            sphere.radius = std::max(sphere.radius, dist);
        }
        sphere.radius = quantizeRadius(sphere.radius);
        return sphere;
    }

    glm::vec3 CascadeShadowCalculator::snapCenterToLightGrid(
        const glm::vec3& frustumCenter,
        const LightSpaceAxes& axes,
        float texelSize)
    {
        float snappedX = snapToTexel(glm::dot(frustumCenter, axes.lightRight), texelSize);
        float snappedY = snapToTexel(glm::dot(frustumCenter, axes.lightUp), texelSize);
        float lightSpaceZ = glm::dot(frustumCenter, axes.lightDir);

        return snappedX * axes.lightRight +
               snappedY * axes.lightUp +
               lightSpaceZ * axes.lightDir;
    }

    CascadeData CascadeShadowCalculator::computeCascadeMatrix(
        const std::array<glm::vec3, 8>& frustumCorners,
        const glm::vec3& lightDirection,
        uint32_t shadowMapResolution)
    {
        CascadeData result{};

        auto sphere = computeFrustumBoundingSphere(frustumCorners);
        auto axes = LightSpaceAxes::fromDirection(lightDirection);

        // Compute stable extent and texel size
        float stableExtent = sphere.radius * 1.15f;
        float stableTexelSize = (2.0f * stableExtent) / static_cast<float>(shadowMapResolution);
        result.texelSize = stableTexelSize;

        // Snap frustum center and build view matrix
        glm::vec3 snappedCenter = snapCenterToLightGrid(sphere.center, axes, stableTexelSize);
        glm::vec3 lightPos = snappedCenter - axes.lightDir * sphere.radius;
        result.viewMatrix = glm::lookAt(lightPos, snappedCenter, axes.lightUp);

        // Compute Z bounds for depth range
        auto zBounds = computeZBounds(result.viewMatrix, frustumCorners, sphere.radius);
        result.nearDistance = zBounds.nearClip;
        result.farDistance = zBounds.farClip;

        // Create orthographic projection with stable sphere-based bounds
        result.projMatrix = glm::orthoRH_ZO(
            -stableExtent, stableExtent,
            -stableExtent, stableExtent,
            zBounds.nearClip, zBounds.farClip
        );

        result.projMatrix[1][1] *= -1.0f;  // Vulkan Y-flip
        result.viewProjMatrix = result.projMatrix * result.viewMatrix;

        return result;
    }

    float CascadeShadowCalculator::snapToTexel(float value, float texelSize)
    {
        if (texelSize <= 0.0f)
        {
            return value;
        }
        // Snap to texel CENTER to match Vulkan's half-pixel sampling convention
        return (std::floor(value / texelSize + 0.5f) - 0.5f) * texelSize;
    }
}
