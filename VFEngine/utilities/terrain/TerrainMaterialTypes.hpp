#pragma once
#include <string>
#include <array>
#include <cstdint>
#include "../asset/AssetRef.hpp"
#include "TerrainHeightBlend.hpp"

namespace terrain
{
    constexpr const char* TERRAIN_MATERIAL_FORMAT_VERSION = "1.0";
    constexpr int MAX_TERRAIN_LAYERS = 32;

    // VK-1609: `Overlay` was never uploaded to the GPU and rendered as a plain linear average, so
    // its slot is reused for the real Linear/HeightBlend selector. Serialization round-trips
    // through the strings below, not the enum values, and a legacy "Overlay" therefore loads as
    // Linear — behaviour-preserving, and it correctly refuses to silently switch existing content
    // onto a new visual path.
    enum class TerrainLayerBlendMode : uint8_t
    {
        Linear = 0,
        HeightBlend = 1
    };

    inline std::string blendModeToString(TerrainLayerBlendMode mode)
    {
        switch (mode)
        {
        case TerrainLayerBlendMode::Linear: return "Linear";
        case TerrainLayerBlendMode::HeightBlend: return "HeightBlend";
        default: return "Linear";
        }
    }

    inline TerrainLayerBlendMode stringToLayerBlendMode(const std::string& str)
    {
        if (str == "HeightBlend") return TerrainLayerBlendMode::HeightBlend;
        return TerrainLayerBlendMode::Linear;
    }

    struct TerrainMaterialLayer
    {
        std::string name;
        asset::AssetRef materialRef;          // PBR source (.vfMat/.vfMatInstance): supplies albedo/normal/ORM textures + roughness/metallic/ao/emission
        float tilingScale = 1.0f;             // Terrain-layer-local UV tiling (not a material concept)
        TerrainLayerBlendMode blendMode = TerrainLayerBlendMode::Linear;
        // VK-1609: sharpness of the height transition, consulted only when blendMode ==
        // HeightBlend AND the layer's source material has a packed ORM (height rides ORM alpha).
        // Clamped to [0, MAX_HEIGHT_BLEND_CONTRAST] on upload; 0 blends linearly.
        float heightContrast = 4.0f;
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
