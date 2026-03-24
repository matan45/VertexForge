#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::shadow
{
    struct ClipmapLevelData
    {
        float worldExtent = 0.0f;    // half-extent in world units (baseExtent * 2^level)
        float texelSize = 0.0f;      // worldExtent * 2 / resolution
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projMatrix{1.0f};
        glm::mat4 viewProjMatrix{1.0f};
        glm::vec2 snapPosition{0.0f}; // light-space XZ of snapped center (for dirty detection)
        float nearDistance = 0.0f;
        float farDistance = 0.0f;
    };

    // Result of page-grid shift computation for toroidal scrolling
    struct PageGridUpdate
    {
        glm::ivec2 pageShift{0};        // how many whole pages the origin shifted
        glm::vec2  newPageGridOrigin{0.0f};
        bool       fullInvalidation = false; // true if shift >= pagesPerSide (teleport)
    };

    class ClipmapShadowCalculator
    {
    public:
        static ClipmapLevelData computeClipmapLevel(
            uint32_t level,
            float baseExtent,
            const glm::vec3& cameraWorldPos,
            const glm::vec3& lightDirection,
            uint32_t resolution);

        // Page-grid-snapped VP: stable across texel snaps, only changes on page-boundary crossings.
        // Used for rendering pages so depth content is world-anchored.
        static ClipmapLevelData computeClipmapLevelStable(
            uint32_t level,
            float baseExtent,
            const glm::vec2& pageGridOrigin,
            float lightSpaceZ,
            const glm::vec3& lightDirection,
            uint32_t resolution);

        // Compute page-grid shift for toroidal scrolling
        static PageGridUpdate computePageGridShift(
            const ClipmapLevelData& levelData,
            uint32_t pagesPerSide,
            const glm::vec2& previousPageGridOrigin);

        // Returns how many texels the center moved since last frame (magnitude)
        static float computeSnapDelta(
            const ClipmapLevelData& current,
            const glm::vec2& previousSnapPosition);

        // Returns per-axis texel shift (for incremental scrolling)
        static glm::ivec2 computeSnapDeltaTexels(
            const ClipmapLevelData& current,
            const glm::vec2& previousSnapPosition);

    private:
        static float snapToTexel(float value, float texelSize);
    };
}
