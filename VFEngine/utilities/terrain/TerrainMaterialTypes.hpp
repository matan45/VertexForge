#pragma once
#include <string>
#include <array>
#include <cstdint>

namespace terrain
{
    constexpr const char* TERRAIN_MATERIAL_FORMAT_VERSION = "1.2";
    constexpr int MAX_TERRAIN_LAYERS = 16;

    enum class TerrainLayerBlendMode : uint8_t
    {
        Linear = 0,
        Overlay = 1
    };

    inline std::string blendModeToString(TerrainLayerBlendMode mode)
    {
        switch (mode)
        {
        case TerrainLayerBlendMode::Linear: return "Linear";
        case TerrainLayerBlendMode::Overlay: return "Overlay";
        default: return "Linear";
        }
    }

    inline TerrainLayerBlendMode stringToLayerBlendMode(const std::string& str)
    {
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

        std::string cachedMaterialSnippet;
        bool needsRecompile = true;
    };
}
