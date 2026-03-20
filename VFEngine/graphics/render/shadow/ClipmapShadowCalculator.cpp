#include "ClipmapShadowCalculator.hpp"
#include "ShadowTypes.hpp"
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

        result.worldExtent = baseExtent * std::pow(2.0f, static_cast<float>(level));

        auto axes = LightSpaceAxes::fromDirection(lightDirection);

        // Texel size must match EXACTLY with projection extent (no margin)
        float diameter = 2.0f * result.worldExtent;
        result.texelSize = diameter / static_cast<float>(resolution);

        // Project camera onto light-space axes
        float lightSpaceX = glm::dot(cameraWorldPos, axes.lightRight);
        float lightSpaceY = glm::dot(cameraWorldPos, axes.lightUp);
        float lightSpaceZ = glm::dot(cameraWorldPos, axes.lightDir);

        // Snap XY to exact texel grid (prevents shimmer)
        float snappedX = snapToTexel(lightSpaceX, result.texelSize);
        float snappedY = snapToTexel(lightSpaceY, result.texelSize);
        result.snapPosition = glm::vec2(snappedX, snappedY);

        // Snap Z to coarse grid (stable depth range, infrequent updates)
        float zSnapGrid = std::max(result.worldExtent, 50.0f);
        float snappedZ = snapToTexel(lightSpaceZ, zSnapGrid);

        glm::vec3 snappedCenter = snappedX * axes.lightRight +
                                   snappedY * axes.lightUp +
                                   snappedZ * axes.lightDir;

        // Z range extends behind camera to capture shadow casters
        float zHalfRange = std::max(result.worldExtent * 4.0f, 1000.0f);
        glm::vec3 lightPos = snappedCenter - axes.lightDir * zHalfRange;
        result.viewMatrix = glm::lookAt(lightPos, snappedCenter, axes.lightUp);

        result.nearDistance = 1.0f;
        result.farDistance = zHalfRange * 2.0f;

        result.projMatrix = glm::orthoRH_ZO(
            -result.worldExtent, result.worldExtent,
            -result.worldExtent, result.worldExtent,
            result.nearDistance, result.farDistance
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
