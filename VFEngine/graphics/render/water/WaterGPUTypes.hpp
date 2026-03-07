#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::water
{
    // Per-tile instance data uploaded to SSBO each frame
    namespace WaterTileFlags
    {
        constexpr uint32_t None     = 0;
        constexpr uint32_t Selected = 1 << 0;
    }

    struct alignas(16) WaterTileGPUData
    {
        glm::vec4 worldOriginAndSize;   // xyz = tile world origin, w = worldTileSize
        glm::vec4 heightAndWave;        // x = waterHeight, y = waveIntensity, z = flags (as float-bits), w = 0
    };
    static_assert(sizeof(WaterTileGPUData) == 32);

    // Push constants for water rendering (must fit 128-byte limit)
    struct WaterPushConstants
    {
        glm::vec4 shallowColor;         // 16 bytes
        glm::vec4 deepColor;            // 16 bytes
        float waveSpeed;                // 4
        float waveAmplitude;            // 4
        float waveFrequency;            // 4
        float maxVisibleDepth;          // 4
        float fresnelPower;             // 4
        float dudvTiling;               // 4
        float dudvStrength;             // 4
        float waveDirection;            // 4 (angle in radians)
    };
    static_assert(sizeof(WaterPushConstants) == 64);

    // Vertex format for the subdivided unit quad
    struct WaterVertex
    {
        glm::vec3 position;             // 12 bytes
        glm::vec2 texCoord;             // 8 bytes
    };
    static_assert(sizeof(WaterVertex) == 20);

    constexpr uint32_t MAX_WATER_TILES = 256;
    constexpr uint32_t WATER_DEFAULT_SUBDIVISIONS = 32;
}
