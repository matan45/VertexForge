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
        result.worldExtent = baseExtent * std::pow(2.0f, static_cast<float>(level));

        // Step 2: Setup stable light coordinate system
        glm::vec3 lightDir = glm::normalize(lightDirection);
        glm::vec3 worldUp = (std::abs(lightDir.y) < 0.99f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(1.0f, 0.0f, 0.0f);

        glm::vec3 lightRight = glm::normalize(glm::cross(worldUp, lightDir));
        glm::vec3 lightUp = glm::cross(lightDir, lightRight);

        // Step 3: Compute texel size
        // Must match EXACTLY with the orthographic projection extent.
        // Projection uses [-worldExtent, worldExtent] = 2*worldExtent total.
        float diameter = 2.0f * result.worldExtent;
        result.texelSize = diameter / static_cast<float>(resolution);

        // Step 4: Project camera onto light-space axes
        float lightSpaceX = glm::dot(cameraWorldPos, lightRight);
        float lightSpaceY = glm::dot(cameraWorldPos, lightUp);
        float lightSpaceZ = glm::dot(cameraWorldPos, lightDir);

        // Step 5: Snap XY to EXACT texel grid (prevents XY shimmer)
        // No power-of-2 quantization - clipmap texel sizes are already clean.
        float snappedX = snapToTexel(lightSpaceX, result.texelSize);
        float snappedY = snapToTexel(lightSpaceY, result.texelSize);

        result.snapPosition = glm::vec2(snappedX, snappedY);

        // Step 6: Snap Z to a coarse grid
        // Z movement doesn't affect XY shadow placement (orthographic projection),
        // but it does shift the depth range. Snapping Z to a large grid ensures
        // the view matrix only changes infrequently along the light direction.
        // The depth shift per snap is zSnapGrid / (far-near) which is negligible.
        float zSnapGrid = std::max(result.worldExtent, 50.0f);
        float snappedZ = snapToTexel(lightSpaceZ, zSnapGrid);

        // Step 7: Reconstruct snapped center in world space
        glm::vec3 snappedCenter = snappedX * lightRight +
                                   snappedY * lightUp +
                                   snappedZ * lightDir;

        // Step 8: Compute Z range - extend behind camera for shadow casters
        float zHalfRange = std::max(result.worldExtent * 4.0f, 1000.0f);

        glm::vec3 lightPos = snappedCenter - lightDir * zHalfRange;
        result.viewMatrix = glm::lookAt(lightPos, snappedCenter, lightUp);

        // Step 9: Fixed near/far relative to the snapped light position
        float nearClip = 1.0f;
        float farClip = zHalfRange * 2.0f;

        result.nearDistance = nearClip;
        result.farDistance = farClip;

        // Step 10: Build orthographic projection
        // Use EXACTLY worldExtent - no margin.
        // Snap grid = texel size = 2*worldExtent/resolution, so each snap
        // jump moves the projection by exactly 1 pixel. Zero shimmer.
        result.projMatrix = glm::orthoRH_ZO(
            -result.worldExtent, result.worldExtent,
            -result.worldExtent, result.worldExtent,
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
