#include "ClipmapShadowCalculator.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <algorithm>
#include <cmath>

namespace render::shadow
{
    ClipmapLevelData ClipmapShadowCalculator::computeClipmapLevel(
        uint32_t level,
        float baseExtent,
        const glm::vec3& cameraWorldPos,
        const glm::vec3& lightDirection,
        uint32_t resolution)
    {
        ClipmapLevelData result{};

        // Step 1: Compute world extent for this level
        // Each level doubles in coverage: level 0 = baseExtent, level 1 = 2x, level 2 = 4x, etc.
        result.worldExtent = baseExtent * std::pow(2.0f, static_cast<float>(level));

        // Step 2: Setup stable light coordinate system
        // CRITICAL: Must be identical to CascadeShadowCalculator to ensure consistent axes
        glm::vec3 lightDir = glm::normalize(lightDirection);
        glm::vec3 worldUp = (std::abs(lightDir.y) < 0.99f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(1.0f, 0.0f, 0.0f);

        glm::vec3 lightRight = glm::normalize(glm::cross(worldUp, lightDir));
        glm::vec3 lightUp = glm::cross(lightDir, lightRight);

        // Step 3: Compute texel size from the extent and resolution
        float diameter = 2.0f * result.worldExtent;
        result.texelSize = diameter / static_cast<float>(resolution);

        // Step 4: Project camera position onto light-space XY plane
        // (lightRight = X, lightUp = Y in the orthographic projection)
        float lightSpaceX = glm::dot(cameraWorldPos, lightRight);
        float lightSpaceY = glm::dot(cameraWorldPos, lightUp);
        float lightSpaceZ = glm::dot(cameraWorldPos, lightDir);

        // Step 5: Snap to texel grid (CRITICAL for shimmer prevention)
        // Quantize the snap grid to power-of-2 for absolute stability,
        // matching what CascadeShadowCalculator does.
        float snapGridSize = result.texelSize;
        if (snapGridSize > 0.0f)
        {
            float log2Grid = std::log2(snapGridSize);
            float quantizedLog = std::ceil(log2Grid);
            snapGridSize = std::pow(2.0f, quantizedLog);
        }
        else
        {
            snapGridSize = 1.0f;
        }

        float snappedX = snapToTexel(lightSpaceX, snapGridSize);
        float snappedY = snapToTexel(lightSpaceY, snapGridSize);

        result.snapPosition = glm::vec2(snappedX, snappedY);

        // Step 6: Reconstruct snapped center in world space
        glm::vec3 snappedCenter = snappedX * lightRight +
                                   snappedY * lightUp +
                                   lightSpaceZ * lightDir;

        // Step 7: Compute Z range
        // Extend far behind camera to capture shadow casters between
        // the light source and the visible scene.
        // Use consistent, generous range to avoid shadows disappearing.
        float zExtension = std::max(result.worldExtent * 3.0f, 800.0f);

        // Build view matrix - position the light far back along lightDir
        glm::vec3 lightPos = snappedCenter - lightDir * zExtension;
        result.viewMatrix = glm::lookAt(lightPos, snappedCenter, lightUp);

        // Step 8: Compute stable Z bounds
        // Quantize near/far to prevent depth precision shifts during movement
        float zQuantization = std::max(10.0f, result.worldExtent * 0.1f);

        float nearClip = 0.1f;
        float farClip = zExtension * 2.0f;

        // Quantize
        nearClip = std::max(0.1f, std::floor(nearClip / zQuantization) * zQuantization);
        farClip = std::ceil(farClip / zQuantization) * zQuantization;
        if (farClip <= nearClip) farClip = nearClip + zQuantization;

        result.nearDistance = nearClip;
        result.farDistance = farClip;

        // Step 9: Build orthographic projection with the level's extent
        // 10% margin compensates for texel snapping offsets (same as CSM)
        float stableExtent = result.worldExtent * 1.1f;

        result.projMatrix = glm::orthoRH_ZO(
            -stableExtent, stableExtent,
            -stableExtent, stableExtent,
            nearClip, farClip
        );

        result.projMatrix[1][1] *= -1.0f;  // Vulkan Y-flip

        result.viewProjMatrix = result.projMatrix * result.viewMatrix;

        return result;
    }

    float ClipmapShadowCalculator::computeSnapDelta(
        const ClipmapLevelData& current,
        const glm::vec2& previousSnapPosition)
    {
        glm::vec2 delta = current.snapPosition - previousSnapPosition;
        if (current.texelSize <= 0.0f) return 0.0f;
        return glm::length(delta) / current.texelSize;
    }

    float ClipmapShadowCalculator::snapToTexel(float value, float texelSize)
    {
        if (texelSize <= 0.0f) return value;
        return std::floor(value / texelSize) * texelSize;
    }
}
