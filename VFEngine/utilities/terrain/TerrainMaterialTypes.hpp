#pragma once
#include <string>
#include <array>
#include <cstdint>
#include "../asset/AssetRef.hpp"
#include "TerrainAntiTiling.hpp"
#include "TerrainHeightBlend.hpp"
#include "TerrainHexTiling.hpp"
#include "TerrainParallax.hpp"
#include "TerrainWeatherResponse.hpp"

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
        // VK-1612 hex-tile stochastic sampling. Per-layer opt-in, default OFF, so the story is
        // provably zero-cost until content asks for it: with no layer opted in, the
        // TERRAIN_HEX_TILING arm is never compiled and the shader is the one shipped before it.
        bool hexTiling = false;
        float hexCellScale = HEX_TILING_DEFAULT_CELL_SCALE;
        float hexContrast = HEX_TILING_DEFAULT_CONTRAST;
        float hexRotation = HEX_TILING_DEFAULT_ROTATION;
        // VK-1614 per-layer weather response. Per-layer opt-in, default OFF, following the VK-1612
        // shape above: with no layer opted in, TERRAIN_WEATHER_RESPONSE is never compiled and the
        // shader is the one shipped before this story.
        //   porosity      - how much water this layer absorbs: drives the wetness darkening and
        //                   suppresses puddling (sand drinks it, rock pools it). Overrides the
        //                   roughness*roughness value common/wetness.glsl derives today.
        //   snowRetention - how much of the global/local snow amount this layer holds. Scales the
        //                   amount, so it composes with (and cannot defeat) the shared slope mask.
        // Both are clamped INTO [MIN_LAYER_WEATHER_SCALAR, 1] on upload, never to 0: 0.0f is
        // reserved as the "did not opt in" sentinel the GPU struct relies on.
        bool weatherResponse = false;
        float porosity = DEFAULT_LAYER_POROSITY;
        float snowRetention = DEFAULT_LAYER_SNOW_RETENTION;
        bool enabled = true;
    };

    // VK-1611. Material-global, not per-layer, and both features default to strength 0 — so a
    // terrain material authored before this story renders bit-identically after it. Sizes are
    // authored in WORLD METRES (the artist-meaningful unit); the reciprocal the shader wants is
    // computed once on upload by macroVariationFrequency().
    struct TerrainAntiTilingSettings
    {
        float macroVariationStrength = 0.0f; // 0 = off; the composite multiplier is exactly 1.0
        float macroVariationSize0 = MACRO_VARIATION_DEFAULT_SIZE0;
        float macroVariationSize1 = MACRO_VARIATION_DEFAULT_SIZE1;
        uint32_t macroVariationSeed = 0;

        // 0 = off, and off means the TERRAIN_DISTANCE_RESCALE permutation is not compiled at all,
        // so the second albedo textureGrad does not exist in the shader. That is the whole reason
        // this one gets a macro while macro variation does not: it costs fetches, not ALU.
        float distanceRescaleStrength = 0.0f;
        float distanceRescaleScale = DISTANCE_RESCALE_DEFAULT_SCALE;
        float distanceRescaleKnee = DISTANCE_RESCALE_DEFAULT_KNEE;
        float distanceRescaleWidth = DISTANCE_RESCALE_DEFAULT_WIDTH;
    };

    struct TerrainMaterialData
    {
        std::string uuid;
        std::string name = "New Terrain Material";
        std::array<TerrainMaterialLayer, MAX_TERRAIN_LAYERS> layers;
        uint8_t activeLayerCount = 1;
        TerrainAntiTilingSettings antiTiling;

        std::string cachedMaterialSnippet;
        bool needsRecompile = true;

        // VK-1625, APPENDED AT THE END DELIBERATELY. This struct crosses a DLL boundary (Serialization
        // includes it) and its size changes here, so a partial rebuild reads it at the wrong offsets.
        // Members declared BEFORE an insertion point still read correctly while everything after it
        // reads garbage — appending at the end means a missed rebuild breaks only the new field and
        // shows up as "parallax settings come back as defaults" instead of corrupting the layer array.
        // See VK-1620's note on ExtractedPBRValues / MaterialData for the diagnostic signature.
        //
        // Material-global rather than per-layer for the same reason antiTiling is: the parallax offset
        // is applied ONCE to the shared base UV that every layer derives from.
        TerrainParallaxSettings parallax;
    };
}
