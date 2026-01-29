#include "CascadeShadowCalculator.hpp"
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

        std::array<glm::vec3, 8> subFrustumCorners;
        for (int i = 0; i < 4; ++i)
        {
            glm::vec3 nearCorner = worldCorners[i];
            glm::vec3 farCorner = worldCorners[i + 4];

            subFrustumCorners[i] = glm::mix(nearCorner, farCorner, tNear);
            subFrustumCorners[i + 4] = glm::mix(nearCorner, farCorner, tFar);
        }

        return subFrustumCorners;
    }

    CascadeData CascadeShadowCalculator::computeCascadeMatrix(
        const std::array<glm::vec3, 8>& frustumCorners,
        const glm::vec3& lightDirection,
        uint32_t shadowMapResolution)
    {
        CascadeData result{};

        glm::vec3 frustumCenter{0.0f};
        for (const auto& corner : frustumCorners)
            frustumCenter += corner;
        frustumCenter /= 8.0f;

        // Bounding sphere radius for rotation-invariant bounds
        float radius = 0.0f;
        for (const auto& corner : frustumCorners)
        {
            float dist = glm::length(corner - frustumCenter);
            radius = std::max(radius, dist);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        glm::vec3 lightDir = glm::normalize(lightDirection);
        glm::vec3 lightUp = (std::abs(lightDir.y) < 0.99f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(1.0f, 0.0f, 0.0f);

        glm::vec3 lightPos = frustumCenter - lightDir * radius;
        result.viewMatrix = glm::lookAt(lightPos, frustumCenter, lightUp);

        glm::vec3 minBounds{std::numeric_limits<float>::max()};
        glm::vec3 maxBounds{std::numeric_limits<float>::lowest()};

        for (const auto& corner : frustumCorners)
        {
            glm::vec3 lightSpaceCorner = glm::vec3(result.viewMatrix * glm::vec4(corner, 1.0f));
            minBounds = glm::min(minBounds, lightSpaceCorner);
            maxBounds = glm::max(maxBounds, lightSpaceCorner);
        }

        float worldUnitsPerTexelX = (maxBounds.x - minBounds.x) / static_cast<float>(shadowMapResolution);
        float worldUnitsPerTexelY = (maxBounds.y - minBounds.y) / static_cast<float>(shadowMapResolution);
        result.texelSize = std::max(worldUnitsPerTexelX, worldUnitsPerTexelY);

        // Snap to texel grid to prevent shadow swimming
        minBounds.x = snapToTexel(minBounds.x, worldUnitsPerTexelX);
        minBounds.y = snapToTexel(minBounds.y, worldUnitsPerTexelY);
        maxBounds.x = snapToTexel(maxBounds.x, worldUnitsPerTexelX);
        maxBounds.y = snapToTexel(maxBounds.y, worldUnitsPerTexelY);

        // Extend Z range to catch shadow casters outside camera frustum
        // In RH light space: minBounds.z is farthest, maxBounds.z is closest
        float zRange = maxBounds.z - minBounds.z;
        float zExtension = std::max(zRange * 2.0f, 500.0f);

        // orthoRH_ZO expects positive near/far distances
        float nearClip = -maxBounds.z;
        float farClip = -minBounds.z + zExtension;

        if (nearClip < 0.01f) nearClip = 0.01f;
        if (farClip <= nearClip) farClip = nearClip + 1.0f;

        result.nearDistance = nearClip;
        result.farDistance = farClip;

        result.projMatrix = glm::orthoRH_ZO(
            minBounds.x, maxBounds.x,
            minBounds.y, maxBounds.y,
            nearClip, farClip
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
        return std::floor(value / texelSize) * texelSize;
    }
}
