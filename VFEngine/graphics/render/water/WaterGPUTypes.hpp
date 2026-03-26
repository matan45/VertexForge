#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::water
{
    // Per-instance data uploaded to SSBO each frame (single ocean plane)
    struct alignas(16) WaterTileGPUData
    {
        glm::vec4 worldOriginAndSize;   // xyz = plane world origin, w = planeSize
        glm::vec4 heightAndWave;        // x = waterHeight, y = 1.0 (wave intensity), z = 0, w = 0
    };
    static_assert(sizeof(WaterTileGPUData) == 32);

    // Push constants for ocean rendering (must fit 128-byte limit)
    struct WaterPushConstants
    {
        glm::vec4 shallowColor;         // 16 bytes
        glm::vec4 deepColor;            // 16 bytes
        float maxVisibleDepth;          // 4
        float fresnelPower;             // 4
        float oceanChoppiness;          // 4
        float oceanPatchSize;           // 4
        float oceanFoamThreshold;       // 4
        float pad0;                     // 4
        float pad1;                     // 4
        float pad2;                     // 4
    };
    static_assert(sizeof(WaterPushConstants) == 64);

    // Vertex format for the subdivided unit quad
    struct WaterVertex
    {
        glm::vec3 position;             // 12 bytes
        glm::vec2 texCoord;             // 8 bytes
    };
    static_assert(sizeof(WaterVertex) == 20);

    constexpr uint32_t WATER_DEFAULT_SUBDIVISIONS = 64;
}
