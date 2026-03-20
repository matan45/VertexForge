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

        // Step 2: Setup stable light coordinate system (same derivation as CascadeShadowCalculator)
        glm::vec3 lightDir = glm::normalize(lightDirection);
        glm::vec3 worldUp = (std::abs(lightDir.y) < 0.99f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(1.0f, 0.0f, 0.0f);

        glm::vec3 lightRight = glm::normalize(glm::cross(worldUp, lightDir));
        glm::vec3 lightUp = glm::cross(lightDir, lightRight);

        // Step 3: Compute texel size
        float diameter = 2.0f * result.worldExtent;
        result.texelSize = diameter / static_cast<float>(resolution);

        // Step 4: Project camera position onto light-space XZ plane
        float lightSpaceX = glm::dot(cameraWorldPos, lightRight);
        float lightSpaceY = glm::dot(cameraWorldPos, lightUp);
        float lightSpaceZ = glm::dot(cameraWorldPos, lightDir);

        // Step 5: Snap to texel grid (prevents shimmering)
        // Use the texel size directly as snap grid - it's already power-of-2 aligned
        // because worldExtent doubles per level and resolution is fixed
        float snappedX = snapToTexel(lightSpaceX, result.texelSize);
        float snappedY = snapToTexel(lightSpaceY, result.texelSize);

        result.snapPosition = glm::vec2(snappedX, snappedY);

        // Step 6: Reconstruct snapped center in world space
        glm::vec3 snappedCenter = snappedX * lightRight +
                                   snappedY * lightUp +
                                   lightSpaceZ * lightDir;

        // Step 7: Build view matrix
        // Z-range: extend far behind camera to capture shadow casters
        // Use a generous range proportional to the level extent
        float zRange = std::max(result.worldExtent * 4.0f, 800.0f);

        glm::vec3 lightPos = snappedCenter - lightDir * zRange;
        result.viewMatrix = glm::lookAt(lightPos, snappedCenter, lightUp);

        // Step 8: Orthographic projection
        float nearClip = 0.1f;
        float farClip = zRange * 2.0f;

        // Quantize near/far for stability
        float zQuantization = std::max(10.0f, result.worldExtent * 0.1f);
        nearClip = std::max(0.1f, std::floor(nearClip / zQuantization) * zQuantization);
        farClip = std::ceil(farClip / zQuantization) * zQuantization;

        if (farClip <= nearClip) farClip = nearClip + zQuantization;

        result.nearDistance = nearClip;
        result.farDistance = farClip;

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
