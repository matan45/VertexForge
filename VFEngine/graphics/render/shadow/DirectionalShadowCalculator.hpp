#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <cmath>

namespace render::shadow
{
    // One concentric, camera-centered orthographic "shell" of a directional-light
    // virtual-shadow-map clipmap. Level i covers 2x the world area of level i-1 at
    // the same virtual page resolution, so resolution falls off with distance like a
    // mipmap. Unlike CSM there are no frustum splits — every level is the same square,
    // re-centered on the camera and texel-snapped for stability.
    struct ClipmapLevel
    {
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projMatrix{1.0f};
        glm::mat4 viewProjMatrix{1.0f};
        float extent = 1.0f; // half-size of the ortho square in world units
        float nearPlane = 0.0f;
        float farPlane = 1.0f;
    };

    // Header-only so the pure math (level extent, texel snapping, level selection) can be
    // unit-tested by the CPU-only Tests project, which includes graphics headers but does
    // not link the Graphics library.
    class DirectionalShadowCalculator
    {
    public:
        // Half-extent of clipmap level i in world units: baseExtent * 2^i.
        static float levelExtent(float baseExtent, uint32_t level)
        {
            return baseExtent * static_cast<float>(1u << level);
        }

        // World units covered by a single shadow texel at the given level.
        static float levelTexelSize(float baseExtent, uint32_t level,
                                    uint32_t pagesPerLevel, uint32_t pageSize)
        {
            float fullExtent = 2.0f * levelExtent(baseExtent, level);
            float texelCount = static_cast<float>(pagesPerLevel * pageSize);
            return (texelCount > 0.0f) ? (fullExtent / texelCount) : fullExtent;
        }

        // Snap a coordinate down to the nearest multiple of texel. Snapping the clipmap
        // center to its own texel grid is the standard fix for camera-move shimmer: the
        // shadow texels stop sliding under sub-texel camera motion.
        static float snapToTexel(float value, float texel)
        {
            if (texel <= 0.0f)
                return value;
            return std::floor(value / texel) * texel;
        }

        // Pick the finest clipmap level whose square contains a point that is
        // planarDistance world units from the camera center (measured in the light's
        // view plane). Mirrors the per-level NDC containment test used on the GPU.
        static uint32_t selectLevel(float planarDistance, float baseExtent, uint32_t levelCount)
        {
            if (levelCount == 0)
                return 0;
            if (planarDistance <= baseExtent || baseExtent <= 0.0f)
                return 0;
            float ratio = planarDistance / baseExtent;
            float lvlf = std::ceil(std::log2(ratio));
            if (lvlf < 0.0f)
                lvlf = 0.0f;
            uint32_t lvl = static_cast<uint32_t>(lvlf);
            if (lvl >= levelCount)
                lvl = levelCount - 1;
            return lvl;
        }

        // Build the per-level orthographic matrices for a directional clipmap centered on
        // the camera. depthRange is how far along the light direction each level's ortho
        // frustum spans (must cover the scene's vertical extent + view distance).
        static std::vector<ClipmapLevel> computeClipmapLevels(
            const glm::vec3& lightDirection,
            const glm::vec3& cameraPosition,
            float baseExtent,
            uint32_t levelCount,
            uint32_t pagesPerLevel,
            uint32_t pageSize,
            float depthRange)
        {
            std::vector<ClipmapLevel> levels;
            levels.reserve(levelCount);

            glm::vec3 dir = glm::normalize(lightDirection);
            // Robust pole handling (matches LightSpaceAxes::fromDirection in ShadowTypes.hpp).
            glm::vec3 worldUp = (std::abs(glm::dot(dir, glm::vec3(0.0f, 1.0f, 0.0f))) < 0.999f)
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 right = glm::normalize(glm::cross(worldUp, dir));
            glm::vec3 up = glm::normalize(glm::cross(dir, right));

            for (uint32_t i = 0; i < levelCount; ++i)
            {
                ClipmapLevel lvl;
                lvl.extent = levelExtent(baseExtent, i);
                float texel = levelTexelSize(baseExtent, i, pagesPerLevel, pageSize);

                // Texel-snap the camera position in the light's right/up basis. The depth
                // axis is left unsnapped (it only affects which slice of depth is centered,
                // not lateral texel alignment).
                float cRight = glm::dot(cameraPosition, right);
                float cUp = glm::dot(cameraPosition, up);
                float cDir = glm::dot(cameraPosition, dir);
                float snappedRight = snapToTexel(cRight, texel);
                float snappedUp = snapToTexel(cUp, texel);
                glm::vec3 snappedCenter = right * snappedRight + up * snappedUp + dir * cDir;

                glm::vec3 eye = snappedCenter - dir * (depthRange * 0.5f);
                lvl.viewMatrix = glm::lookAt(eye, eye + dir, up);

                lvl.nearPlane = 0.0f;
                lvl.farPlane = depthRange;
                lvl.projMatrix = glm::ortho(-lvl.extent, lvl.extent,
                                            -lvl.extent, lvl.extent,
                                            lvl.nearPlane, lvl.farPlane);
                lvl.projMatrix[1][1] *= -1.0f; // Vulkan NDC Y-flip (matches SpotShadowCalculator)
                lvl.viewProjMatrix = lvl.projMatrix * lvl.viewMatrix;
                levels.push_back(lvl);
            }
            return levels;
        }
    };
}
