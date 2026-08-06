#pragma once
#include "TerrainWeightMap.hpp"

#include <algorithm>
#include <cstdint>

// VK-1613 per-layer visibility (the terrain material's per-layer `enabled` checkbox).
//
// Hiding a layer is expressed as "its splat weight is zero", because that is already the terrain
// composite's own vocabulary for "this channel is not here": the generated loop culls at
// `w < 0.001` and normalizes by the accumulated weight, so zeroing one channel both removes the
// layer and redistributes what is left across the others. Nothing in the shader or in
// TerrainLayerGPUData has to change, and the live path and the RVT bake stay in agreement for free
// because both read the same weight buffer.
//
// The zeroing happens where the weight bytes are packed for upload (TerrainGPUAdapter), NOT in the
// weight map itself — the painted data is the artist's work and must survive a hide/unhide round
// trip untouched.
//
// This lives in the Terrain DLL as header-only inline code so the Tests project can cover it:
// Tests cannot reach TerrainGPUAdapter (it needs a Vulkan device), but the mask arithmetic and the
// palette indirection are the parts that can actually be got wrong.

namespace terrain
{
    // One bit per layer, bit i == "layer i is visible".
    inline constexpr uint32_t ALL_TERRAIN_LAYERS_ENABLED = 0xFFFFFFFFu;

    static_assert(MAX_TERRAIN_LAYERS <= 32,
                  "the layer visibility mask is a uint32_t, one bit per layer — widen it (or switch "
                  "to a bitset) before raising MAX_TERRAIN_LAYERS past 32");

    // Builds the visibility mask for a material.
    //
    // Starts from all-ones and only ever CLEARS bits, which buys two properties worth stating:
    //  1. A material with nothing hidden — and equally, no material at all — yields exactly
    //     ALL_TERRAIN_LAYERS_ENABLED, so the masked pack loop is provably a no-op for every project
    //     that does not use the feature.
    //  2. Bits at or above activeLayerCount stay set. A tile's palette may legitimately reference a
    //     channel beyond the active count (see TileWeightMapData::layerIndices), and those channels
    //     keep behaving exactly as they do today instead of quietly acquiring a new meaning.
    [[nodiscard]] inline uint32_t buildLayerEnabledMask(const TerrainMaterialData& material) noexcept
    {
        // activeLayerCount is a uint8_t and therefore not structurally bounded by the array size.
        const size_t count = std::min<size_t>(material.activeLayerCount, MAX_TERRAIN_LAYERS);

        uint32_t mask = ALL_TERRAIN_LAYERS_ENABLED;
        for (size_t i = 0; i < count; ++i)
        {
            if (!material.layers[i].enabled)
            {
                mask &= ~(1u << i);
            }
        }
        return mask;
    }

    // Palette indices are uint8_t, so they can exceed the mask's 32 bits; `1u << layerIndex` would
    // then be undefined. Such an index cannot name a real layer at all, so it is reported visible —
    // the same "leave today's behaviour alone" rule the mask itself follows.
    [[nodiscard]] inline bool isLayerEnabled(uint32_t mask, uint8_t layerIndex) noexcept
    {
        if (layerIndex >= MAX_TERRAIN_LAYERS)
        {
            return true;
        }
        return (mask & (1u << layerIndex)) != 0u;
    }

    // Resolves a weight-map CHANNEL through the tile's palette indirection. Callers should hoist
    // this out of their per-texel loop — it is per channel, and there are only eight of them.
    [[nodiscard]] inline bool isWeightChannelEnabled(const TileWeightMapData& weightMap,
                                                     uint8_t channel, uint32_t mask) noexcept
    {
        if (channel >= WEIGHT_CHANNELS)
        {
            return true;
        }
        return isLayerEnabled(mask, weightMap.layerIndices[channel]);
    }
}
