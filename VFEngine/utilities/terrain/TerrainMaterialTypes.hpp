#pragma once
#include <string>
#include <array>
#include <cstdint>
#include "../material/MaterialTypes.hpp"

namespace terrain
{
    constexpr const char* TERRAIN_MATERIAL_FORMAT_VERSION = "1.2";
    constexpr int MAX_TERRAIN_LAYERS = 16;

    enum class TerrainLayerBlendMode : uint8_t
    {
        Linear = 0,      // Standard weighted linear blend
        HeightBased = 1,  // Height-aware sharp transitions
        Overlay = 2       // Detail overlay
    };

    inline std::string blendModeToString(TerrainLayerBlendMode mode)
    {
        switch (mode)
        {
        case TerrainLayerBlendMode::Linear: return "Linear";
        case TerrainLayerBlendMode::HeightBased: return "HeightBased";
        case TerrainLayerBlendMode::Overlay: return "Overlay";
        default: return "Linear";
        }
    }

    inline TerrainLayerBlendMode stringToLayerBlendMode(const std::string& str)
    {
        if (str == "HeightBased") return TerrainLayerBlendMode::HeightBased;
        if (str == "Overlay") return TerrainLayerBlendMode::Overlay;
        return TerrainLayerBlendMode::Linear;
    }

    struct TerrainMaterialLayer
    {
        std::string name;
        std::string albedoTexturePath;
        std::string normalTexturePath;
        float tilingScale = 1.0f;
        TerrainLayerBlendMode blendMode = TerrainLayerBlendMode::Linear;
        bool enabled = true;
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
