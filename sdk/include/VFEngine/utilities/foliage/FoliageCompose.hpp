#pragma once
// VK-1573: pure, Vulkan-free helpers used by the foliage instanced render collector.
// Kept header-only and dependency-light (glm + FoliageTypes only) so the CPU-testable
// math (tint unpack, model-matrix compose, tile cull, type grouping) can be exercised by
// the doctest Tests target without dragging in graphics/Vulkan headers. The graphics-side
// adapter (FramePreparationSystem::collectFoliage) wraps these into MeshRenderData records.
#include "FoliageTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace foliage
{
    // Unpack a packed RGBA8 tint into a normalized linear vec4.
    // Byte order is 0xRRGGBBAA (R in the most-significant byte). 0xFFFFFFFF => {1,1,1,1}
    // (opaque white = no tint). Authoring (VK-1575) MUST write the same byte order.
    inline glm::vec4 unpackTintRGBA8(uint32_t tint)
    {
        return glm::vec4(
            static_cast<float>((tint >> 24) & 0xFFu) / 255.0f,
            static_cast<float>((tint >> 16) & 0xFFu) / 255.0f,
            static_cast<float>((tint >> 8) & 0xFFu) / 255.0f,
            static_cast<float>(tint & 0xFFu) / 255.0f);
    }

    // Compose the world model matrix for one foliage instance. TRS built as
    // Translate * (align-to-normal) * RotateY * Scale. The instance already stores final
    // world-space position/rotationY/scale/normal (baked at authoring time); alignToNormal
    // is a per-type toggle that tilts the plant's up-axis onto the surface normal.
    inline glm::mat4 composeFoliageModelMatrix(const FoliageInstance& fi, const FoliageType& type)
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), fi.position);

        if (type.alignToNormal)
        {
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            const glm::vec3 n = glm::normalize(fi.normal);
            const float d = glm::dot(up, n);
            if (d < 0.9999f)
            {
                if (d <= -0.9999f)
                {
                    // Normal points straight down: 180 deg about an arbitrary perpendicular axis.
                    m = glm::rotate(m, glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
                }
                else
                {
                    const glm::vec3 axis = glm::normalize(glm::cross(up, n));
                    const float angle = std::acos(glm::clamp(d, -1.0f, 1.0f));
                    m = glm::rotate(m, angle, axis);
                }
            }
        }

        m = glm::rotate(m, fi.rotationY, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::scale(m, fi.scale);
        return m;
    }

    // 2D XZ camera-distance test (mirrors the billboard/grass per-tile cull). Returns true
    // when the tile center is within cullDist of the camera on the XZ plane.
    inline bool foliageTileInRange(const glm::vec3& tileCenter, const glm::vec3& cameraPos, float cullDist)
    {
        const float dx = cameraPos.x - tileCenter.x;
        const float dz = cameraPos.z - tileCenter.z;
        return (dx * dx + dz * dz) <= (cullDist * cullDist);
    }

    // Group instance indices by typeIndex. Instances whose typeIndex is out of the palette
    // range (>= paletteSize) are dropped so the collector never indexes past the palette.
    inline std::unordered_map<uint16_t, std::vector<uint32_t>>
    groupInstanceIndicesByType(const std::vector<FoliageInstance>& instances, size_t paletteSize)
    {
        std::unordered_map<uint16_t, std::vector<uint32_t>> groups;
        for (uint32_t i = 0; i < instances.size(); ++i)
        {
            const uint16_t t = instances[i].typeIndex;
            if (static_cast<size_t>(t) >= paletteSize) continue;
            groups[t].push_back(i);
        }
        return groups;
    }
}
