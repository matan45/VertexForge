#pragma once

#include "TerrainMaterialTypes.hpp"
#include <vector>
#include <array>
#include <cstdint>

namespace terrain
{
    static constexpr uint8_t WEIGHT_CHANNELS = 4;

    struct TileWeightMapData
    {
        // layerWeights[channel] = flat vector of size resolution*resolution
        // Values in [0.0, 1.0], sum across channels at each texel should be 1.0
        std::vector<std::vector<float>> layerWeights;
        uint32_t resolution = 0; // matches tile vertex count (33, 65, 129)

        // Per-tile palette indirection: channel N maps to palette layer layerIndices[N]
        std::array<uint8_t, WEIGHT_CHANNELS> layerIndices = {0, 1, 2, 3};

        [[nodiscard]] bool isInitialized() const { return resolution > 0 && !layerWeights.empty(); }
        [[nodiscard]] size_t getTexelCount() const { return static_cast<size_t>(resolution) * resolution; }

        [[nodiscard]] float getWeight(uint32_t channel, uint32_t x, uint32_t z) const;
        void setWeight(uint32_t channel, uint32_t x, uint32_t z, float value);

        void normalizeAt(uint32_t x, uint32_t z);
        void normalizeAll();
        void initializeDefault(uint32_t vertexResolution);

        void packRGBA(uint32_t x, uint32_t z,
                      float& r, float& g, float& b, float& a) const;

        // Returns channel [0-3] if paletteLayer is assigned, else 0xFF
        [[nodiscard]] uint8_t findChannel(uint8_t paletteLayer) const;

        // Assigns paletteLayer to a free or least-used channel. Returns channel index.
        uint8_t assignChannel(uint8_t paletteLayer);
    };
}
