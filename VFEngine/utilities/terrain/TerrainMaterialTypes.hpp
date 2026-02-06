#pragma once
#include <string>
#include <array>
#include <cstdint>
#include "../material/MaterialTypes.hpp"

namespace terrain
{
    constexpr const char* TERRAIN_MATERIAL_FORMAT_VERSION = "1.1";
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

        // Shader graph for terrain material authoring
        material::ShaderGraph graph;
        std::string cachedMaterialSnippet; // Compiled GLSL snippet
        bool needsRecompile = true;
    };
}
