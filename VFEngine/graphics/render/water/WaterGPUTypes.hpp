#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::water
{
    constexpr uint32_t WATER_LOD_COUNT = 4;
    constexpr uint32_t WATER_LOD0_SUBDIVISIONS = 64;
    constexpr uint32_t WATER_LOD1_SUBDIVISIONS = 32;
    constexpr uint32_t WATER_LOD2_SUBDIVISIONS = 16;
    constexpr uint32_t WATER_LOD3_SUBDIVISIONS = 8;

    // Per-tile instance data uploaded to SSBO each frame
    struct alignas(16) WaterTileGPUData
    {
        glm::vec4 worldOriginAndSize;   // xyz = tile world origin, w = tileSize
        glm::vec4 heightAndWave;        // x = waterHeight, y = 1.0, z = lodLevel (as float), w = 0
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
        float oceanPatchSize0;          // 4  (swell band patch size)
        float oceanFoamThreshold;       // 4
        float refractionStrength;       // 4
        float refractionChromatic;      // 4
        float refractionDepthScale;     // 4
        float oceanPatchSize1;          // 4  (agitation band patch size)
        float oceanPatchSize2;          // 4  (ripples band patch size)
        uint32_t bandEnableMask;        // 4  (bit 0=swell, bit 1=agitation, bit 2=ripples)
        float pad;                      // 4
    };
    static_assert(sizeof(WaterPushConstants) == 80);

    // Vertex format for the subdivided unit quad
    struct WaterVertex
    {
        glm::vec3 position;             // 12 bytes
        glm::vec2 texCoord;             // 8 bytes
    };
    static_assert(sizeof(WaterVertex) == 20);

    constexpr uint32_t MAX_OCEAN_GPU_INSTANCES = 128;
    constexpr uint32_t WATER_DEFAULT_SUBDIVISIONS = 64;

    // Per-LOD mesh info (returned by WaterMeshBuffer)
    struct WaterLODMeshInfo
    {
        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
        uint32_t subdivisions = 0;
    };
}
