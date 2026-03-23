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

    class ClipmapShadowCalculator
    {
    public:
        static ClipmapLevelData computeClipmapLevel(
            uint32_t level,
            float baseExtent,
            const glm::vec3& cameraWorldPos,
            const glm::vec3& lightDirection,
            uint32_t resolution);

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
