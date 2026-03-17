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

        // Step 1: Compute frustum center
        glm::vec3 frustumCenter{0.0f};
        for (const auto& corner : frustumCorners)
            frustumCenter += corner;
        frustumCenter /= 8.0f;

        // Step 2: Compute bounding sphere radius for rotation-invariant bounds
        float radius = 0.0f;
        for (const auto& corner : frustumCorners)
        {
            float dist = glm::length(corner - frustumCenter);
            radius = std::max(radius, dist);
        }

        // Quantize radius to LARGE steps for temporal stability
        // Using power-of-2 buckets ensures radius only changes when cascade size roughly doubles
        // This prevents "breathing" from small floating-point variations
        if (radius > 0.0f)
        {
            float log2Radius = std::log2(radius);
            // Round up to nearest 0.5 in log space (i.e., sqrt(2) multiplier buckets)
            // This gives ~41% size increments, which is coarse enough to be stable
            float quantizedLog = std::ceil(log2Radius * 2.0f) / 2.0f;
            radius = std::pow(2.0f, quantizedLog);
        }
        else
        {
            radius = 1.0f;  // Fallback for degenerate cases
        }

        // Step 3: Setup light coordinate system (world-anchored, not frustum-centered)
        glm::vec3 lightDir = glm::normalize(lightDirection);
        glm::vec3 worldUp = (std::abs(lightDir.y) < 0.99f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(1.0f, 0.0f, 0.0f);

        // Compute stable light-space axes (based only on light direction, not frustum)
        glm::vec3 lightRight = glm::normalize(glm::cross(worldUp, lightDir));
        glm::vec3 lightUp = glm::cross(lightDir, lightRight);

        // Step 4: Compute texel size from sphere diameter
        float sphereDiameter = 2.0f * radius;
        float stableTexelSize = sphereDiameter / static_cast<float>(shadowMapResolution);
        result.texelSize = stableTexelSize;

        // Step 5: SNAP frustum center to WORLD-ANCHORED grid in light space
        // Project frustum center onto light-space axes (relative to world origin 0,0,0)
        float lightSpaceX = glm::dot(frustumCenter, lightRight);
        float lightSpaceY = glm::dot(frustumCenter, lightUp);
        float lightSpaceZ = glm::dot(frustumCenter, lightDir);

        // CRITICAL: Use a FIXED snap grid that doesn't depend on the current texel size
        // The snap grid must be stable even when radius/texelSize changes
        // Snap to the texel size, but quantize the texel size itself to a power of 2
        // This ensures the snap grid spacing is always consistent
        float snapGridSize = stableTexelSize;
        if (snapGridSize > 0.0f)
        {
            // Quantize snap grid to power-of-2 for absolute stability
            float log2Grid = std::log2(snapGridSize);
            float quantizedLog = std::ceil(log2Grid);  // Round up to next power of 2
            snapGridSize = std::pow(2.0f, quantizedLog);
        }
        else
        {
            snapGridSize = 1.0f;
        }

        // Snap X and Y to the stable grid
        float snappedX = snapToTexel(lightSpaceX, snapGridSize);
        float snappedY = snapToTexel(lightSpaceY, snapGridSize);

        // Step 6: Reconstruct snapped position in world space
        glm::vec3 snappedFrustumCenter = snappedX * lightRight +
                                          snappedY * lightUp +
                                          lightSpaceZ * lightDir;

        // Step 7: Build view matrix with snapped center
        glm::vec3 lightPos = snappedFrustumCenter - lightDir * radius;
        result.viewMatrix = glm::lookAt(lightPos, snappedFrustumCenter, lightUp);

        // Step 8: Compute Z bounds for depth range
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();
        for (const auto& corner : frustumCorners)
        {
            glm::vec3 lightSpaceCorner = glm::vec3(result.viewMatrix * glm::vec4(corner, 1.0f));
            minZ = std::min(minZ, lightSpaceCorner.z);
            maxZ = std::max(maxZ, lightSpaceCorner.z);
        }

        // Step 9: Use SPHERE-BASED stable XY bounds instead of tight AABB
        // This prevents scale changes when the frustum rotates.
        // 10% margin compensates for texel snapping offsets that can shift the
        // frustum center by up to half a texel, which at coarser VSM page
        // resolutions (e.g. 512px) is enough to clip shadow casters at edges.
        float stableExtent = radius * 1.1f;

        // Step 10: Stabilize Z range to prevent depth precision shifts during movement
        // Quantize Z bounds to reduce frame-to-frame variation
        float zRange = maxZ - minZ;
        // Extend Z far behind the camera frustum to capture shadow casters that
        // are between the light source and the visible scene. 3× the frustum depth
        // and a minimum of 800 units prevents shadows from disappearing when the
        // camera views objects from the opposite side of the light direction.
        float zExtension = std::max(zRange * 3.0f, 800.0f);

        // Quantize Z bounds to large steps (10 unit increments) for stability
        // This prevents shadow acne/peter-panning changes as camera moves
        float zQuantization = 10.0f;
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

        result.nearDistance = nearClip;
        result.farDistance = farClip;

        // Step 11: Create orthographic projection with stable sphere-based bounds
        result.projMatrix = glm::orthoRH_ZO(
            -stableExtent, stableExtent,
            -stableExtent, stableExtent,
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
