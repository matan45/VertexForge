#include "ClipmapShadowCalculator.hpp"
#include "ShadowTypes.hpp"
#include "VSMTypes.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <algorithm>
#include <cmath>

namespace render::shadow
{
    // Minimum Z-snap grid size in meters — prevents sub-meter Z oscillation on small levels
    static constexpr float MIN_Z_SNAP_METERS = 5.0f;
    // Minimum depth half-range — ensures shadow casters behind camera are captured even for small levels
    static constexpr float MIN_DEPTH_HALF_RANGE_METERS = 1000.0f;
    // Depth range multiplier — each level covers N× its extent in the light direction
    static constexpr float DEPTH_RANGE_EXTENT_MULTIPLIER = 4.0f;

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

        // Z is NOT snapped — for directional lights with orthographic projection,
        // the depth range is large enough that Z movement doesn't cause shimmer.
        // Snapping Z causes visible "jumping" when the camera crosses snap boundaries
        // because the entire depth range shifts, changing every pixel's shadow depth.
        glm::vec3 snappedCenter = snappedX * axes.lightRight +
                                   snappedY * axes.lightUp +
                                   lightSpaceZ * axes.lightDir;

        float zHalfRange = std::max(result.worldExtent * DEPTH_RANGE_EXTENT_MULTIPLIER,
                                     MIN_DEPTH_HALF_RANGE_METERS);
        glm::vec3 lightPos = snappedCenter - axes.lightDir * zHalfRange;
        result.viewMatrix = glm::lookAt(lightPos, snappedCenter, axes.lightUp);

        result.nearDistance = std::max(1.0f, result.worldExtent * 0.1f);
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

    ClipmapLevelData ClipmapShadowCalculator::computeClipmapLevelStable(
        uint32_t level,
        float baseExtent,
        const glm::vec2& pageGridOrigin,
        float lightSpaceZ,
        const glm::vec3& lightDirection,
        uint32_t resolution)
    {
        ClipmapLevelData result{};

        result.worldExtent = baseExtent * std::pow(2.0f, static_cast<float>(level));

        auto axes = LightSpaceAxes::fromDirection(lightDirection);

        float diameter = 2.0f * result.worldExtent;
        result.texelSize = diameter / static_cast<float>(resolution);

        // Use page-grid-snapped origin — VP only changes on page-boundary crossings
        float snappedX = pageGridOrigin.x;
        float snappedY = pageGridOrigin.y;
        result.snapPosition = glm::vec2(snappedX, snappedY);

        glm::vec3 snappedCenter = snappedX * axes.lightRight +
                                   snappedY * axes.lightUp +
                                   lightSpaceZ * axes.lightDir;

        float zHalfRange = std::max(result.worldExtent * DEPTH_RANGE_EXTENT_MULTIPLIER,
                                     MIN_DEPTH_HALF_RANGE_METERS);
        glm::vec3 lightPos = snappedCenter - axes.lightDir * zHalfRange;
        result.viewMatrix = glm::lookAt(lightPos, snappedCenter, axes.lightUp);

        result.nearDistance = std::max(1.0f, result.worldExtent * 0.1f);
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

    PageGridUpdate ClipmapShadowCalculator::computePageGridShift(
        const ClipmapLevelData& levelData,
        uint32_t pagesPerSide,
        const glm::vec2& previousPageGridOrigin)
    {
        PageGridUpdate result{};
        float pageExtent = levelData.texelSize * static_cast<float>(vsm::PAGE_SIZE);
        if (pageExtent <= 0.0f) return result;

        // Snap camera to page-grid boundaries (coarser than texel grid)
        float pgX = snapToTexel(levelData.snapPosition.x, pageExtent);
        float pgY = snapToTexel(levelData.snapPosition.y, pageExtent);
        result.newPageGridOrigin = glm::vec2(pgX, pgY);

        // Compute shift in whole-page units
        glm::vec2 delta = result.newPageGridOrigin - previousPageGridOrigin;
        result.pageShift = glm::ivec2(
            static_cast<int>(std::round(delta.x / pageExtent)),
            static_cast<int>(std::round(delta.y / pageExtent))
        );

        int pps = static_cast<int>(pagesPerSide);
        result.fullInvalidation = (std::abs(result.pageShift.x) >= pps ||
                                   std::abs(result.pageShift.y) >= pps);
        return result;
    }

    glm::ivec2 ClipmapShadowCalculator::computeSnapDeltaTexels(
        const ClipmapLevelData& current,
        const glm::vec2& previousSnapPosition)
    {
        if (current.texelSize <= 0.0f) return glm::ivec2(0);
        glm::vec2 delta = current.snapPosition - previousSnapPosition;
        return glm::ivec2(
            static_cast<int>(std::round(delta.x / current.texelSize)),
            static_cast<int>(std::round(delta.y / current.texelSize))
        );
    }

    float ClipmapShadowCalculator::snapToTexel(float value, float texelSize)
    {
        if (texelSize <= 0.0f) return value;
        // Snap to texel CENTER (not corner) to match Vulkan's half-pixel sampling offset.
        // Without the +0.5, the snap grid sits on texel boundaries, and the hardware
        // sampler at the boundary can oscillate between two texels due to FP rounding.
        return (std::floor(value / texelSize + 0.5f) - 0.5f) * texelSize;
    }
}
