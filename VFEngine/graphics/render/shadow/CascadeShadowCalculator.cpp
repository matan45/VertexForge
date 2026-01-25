#include "CascadeShadowCalculator.hpp"
#include <glm/gtc/matrix_transform.hpp>
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
        // Clamp cascade count to valid range
        cascadeCount = std::clamp(cascadeCount, 1u, 4u);

        std::vector<float> splits(cascadeCount + 1);
        splits[0] = cameraNear;
        splits[cascadeCount] = cameraFar;

        // Avoid division by zero
        if (cameraNear <= 0.0f)
        {
            cameraNear = 0.01f;
        }

        float range = cameraFar - cameraNear;
        float ratio = cameraFar / cameraNear;

        for (uint32_t i = 1; i < cascadeCount; ++i)
        {
            float p = static_cast<float>(i) / static_cast<float>(cascadeCount);

            switch (splitMode)
            {
                case types::CascadeSplitMode::Linear:
                    // Linear distribution: even splits in world space
                    splits[i] = cameraNear + range * p;
                    break;

                case types::CascadeSplitMode::Logarithmic:
                    // Logarithmic distribution: more detail near camera
                    // C_log(i) = near * (far/near)^(i/n)
                    // Using exp(log(ratio) * p) for better numerical stability with large ratios
                    splits[i] = cameraNear * std::exp(std::log(ratio) * p);
                    break;

                case types::CascadeSplitMode::Practical:
                default:
                    // Practical split scheme (GPU Gems 3, Chapter 10)
                    // Blends logarithmic and linear: lambda * log + (1-lambda) * linear
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
        // Inverse view-projection to transform from NDC to world space
        glm::mat4 invViewProj = glm::inverse(cameraProjection * cameraView);

        // NDC corners for Vulkan coordinate system
        // Near plane: z = 0, Far plane: z = 1
        // Y is inverted in Vulkan, but the projection already handles this
        constexpr std::array<glm::vec4, 8> ndcCorners = {{
            {-1.0f, -1.0f, 0.0f, 1.0f}, // near bottom-left
            { 1.0f, -1.0f, 0.0f, 1.0f}, // near bottom-right
            { 1.0f,  1.0f, 0.0f, 1.0f}, // near top-right
            {-1.0f,  1.0f, 0.0f, 1.0f}, // near top-left
            {-1.0f, -1.0f, 1.0f, 1.0f}, // far bottom-left
            { 1.0f, -1.0f, 1.0f, 1.0f}, // far bottom-right
            { 1.0f,  1.0f, 1.0f, 1.0f}, // far top-right
            {-1.0f,  1.0f, 1.0f, 1.0f}, // far top-left
        }};

        // Transform NDC corners to world space
        std::array<glm::vec3, 8> worldCorners;
        for (size_t i = 0; i < 8; ++i)
        {
            glm::vec4 worldPos = invViewProj * ndcCorners[i];
            worldCorners[i] = glm::vec3(worldPos) / worldPos.w;
        }

        // Extract camera position efficiently from view matrix
        // For view matrix V = [R | -R*eye], camera position = -R^T * translation
        // Using vec3 * mat3 (row-vector multiplication) gives us -t * R = -R^T * t
        glm::vec3 cameraPos = -glm::vec3(cameraView[3]) * glm::mat3(cameraView);

        // Compute centers of near and far planes of the full frustum
        glm::vec3 nearCenter = (worldCorners[0] + worldCorners[1] + worldCorners[2] + worldCorners[3]) * 0.25f;
        glm::vec3 farCenter = (worldCorners[4] + worldCorners[5] + worldCorners[6] + worldCorners[7]) * 0.25f;

        // Calculate the full frustum depth (distance from camera to near/far centers)
        float fullNear = glm::length(nearCenter - cameraPos);
        float fullFar = glm::length(farCenter - cameraPos);
        float fullRange = fullFar - fullNear;

        // Calculate interpolation factors for the sub-frustum
        float tNear = (fullRange > 0.0001f) ? (nearDist - fullNear) / fullRange : 0.0f;
        float tFar = (fullRange > 0.0001f) ? (farDist - fullNear) / fullRange : 1.0f;

        // Clamp to valid range
        tNear = std::clamp(tNear, 0.0f, 1.0f);
        tFar = std::clamp(tFar, 0.0f, 1.0f);

        // Interpolate corners to get sub-frustum
        std::array<glm::vec3, 8> subFrustumCorners;
        for (int i = 0; i < 4; ++i)
        {
            glm::vec3 nearCorner = worldCorners[i];
            glm::vec3 farCorner = worldCorners[i + 4];

            subFrustumCorners[i] = glm::mix(nearCorner, farCorner, tNear);     // New near plane
            subFrustumCorners[i + 4] = glm::mix(nearCorner, farCorner, tFar);  // New far plane
        }

        return subFrustumCorners;
    }

    CascadeData CascadeShadowCalculator::computeCascadeMatrix(
        const std::array<glm::vec3, 8>& frustumCorners,
        const glm::vec3& lightDirection,
        uint32_t shadowMapResolution)
    {
        CascadeData result{};

        // 1. Compute frustum center (average of all 8 corners)
        glm::vec3 frustumCenter{0.0f};
        for (const auto& corner : frustumCorners)
        {
            frustumCenter += corner;
        }
        frustumCenter /= 8.0f;

        // 2. Compute bounding sphere radius for rotation-invariant bounds
        // Using sphere instead of AABB ensures stable bounds when camera rotates
        float radius = 0.0f;
        for (const auto& corner : frustumCorners)
        {
            float dist = glm::length(corner - frustumCenter);
            radius = std::max(radius, dist);
        }

        // Round up radius to avoid shadow edge clipping
        // This also helps with stability
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // 3. Build light view matrix
        // Choose an up vector that isn't parallel to light direction
        glm::vec3 lightDir = glm::normalize(lightDirection);
        glm::vec3 lightUp = (std::abs(lightDir.y) < 0.99f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(1.0f, 0.0f, 0.0f);

        // Position light far enough back to encompass the frustum
        glm::vec3 lightPos = frustumCenter - lightDir * radius;
        result.viewMatrix = glm::lookAt(lightPos, frustumCenter, lightUp);

        // 4. Transform frustum corners to light space for AABB computation
        glm::vec3 minBounds{std::numeric_limits<float>::max()};
        glm::vec3 maxBounds{std::numeric_limits<float>::lowest()};

        for (const auto& corner : frustumCorners)
        {
            glm::vec3 lightSpaceCorner = glm::vec3(result.viewMatrix * glm::vec4(corner, 1.0f));
            minBounds = glm::min(minBounds, lightSpaceCorner);
            maxBounds = glm::max(maxBounds, lightSpaceCorner);
        }

        // 5. Compute texel size for stable snapping
        float worldUnitsPerTexelX = (maxBounds.x - minBounds.x) / static_cast<float>(shadowMapResolution);
        float worldUnitsPerTexelY = (maxBounds.y - minBounds.y) / static_cast<float>(shadowMapResolution);
        result.texelSize = std::max(worldUnitsPerTexelX, worldUnitsPerTexelY);

        // 6. Snap bounds to texel grid (THE KEY to preventing shadow swimming)
        // This ensures the shadow map doesn't sub-pixel shift when camera moves
        minBounds.x = snapToTexel(minBounds.x, worldUnitsPerTexelX);
        minBounds.y = snapToTexel(minBounds.y, worldUnitsPerTexelY);
        maxBounds.x = snapToTexel(maxBounds.x, worldUnitsPerTexelX);
        maxBounds.y = snapToTexel(maxBounds.y, worldUnitsPerTexelY);

        // 7. Extend Z range to catch shadow casters outside camera frustum
        // Objects behind the camera might still cast shadows into the visible area
        float zRange = maxBounds.z - minBounds.z;
        float zNear = minBounds.z - zRange * 0.5f;  // Extend back
        float zFar = maxBounds.z;

        result.nearDistance = zNear;
        result.farDistance = zFar;

        // 8. Build orthographic projection matrix
        // glm::ortho creates: left, right, bottom, top, near, far
        result.projMatrix = glm::ortho(
            minBounds.x, maxBounds.x,  // left, right
            minBounds.y, maxBounds.y,  // bottom, top
            zNear, zFar                // near, far
        );

        // 9. Flip Y for Vulkan coordinate system
        // Vulkan has Y pointing down in NDC, OpenGL has Y pointing up
        result.projMatrix[1][1] *= -1.0f;

        // 10. Combine view and projection
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
