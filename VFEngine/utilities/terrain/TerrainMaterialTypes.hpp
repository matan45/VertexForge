#pragma once
#include <string>
#include <array>
#include <cstdint>

namespace terrain
{
    constexpr const char* TERRAIN_MATERIAL_FORMAT_VERSION = "1.0";
    constexpr int MAX_TERRAIN_LAYERS = 16;

    struct TerrainMaterialLayer
    {
        std::string albedoTexturePath;
        std::string normalTexturePath;
        float tilingScale = 1.0f;
    };

    struct TerrainMaterialData
    {
        std::string uuid;
        std::string name = "New Terrain Material";
        std::array<TerrainMaterialLayer, MAX_TERRAIN_LAYERS> layers;
        uint8_t activeLayerCount = 1;
    };
}
