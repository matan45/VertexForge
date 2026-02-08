#pragma once

#include "TerrainMaterialTypes.hpp"
#include <vector>
#include <cstdint>

namespace terrain
{
    struct TileWeightMapData
    {
        // layerWeights[layerIndex] = flat vector of size resolution*resolution
        // Values in [0.0, 1.0], sum across layers at each texel should be 1.0
        std::vector<std::vector<float>> layerWeights;
        uint32_t resolution = 0; // matches tile vertex count (33, 65, 129)
        uint8_t activeLayerCount = 0;

        [[nodiscard]] bool isInitialized() const { return resolution > 0 && !layerWeights.empty(); }
        [[nodiscard]] size_t getTexelCount() const { return static_cast<size_t>(resolution) * resolution; }

        [[nodiscard]] float getWeight(uint32_t layerIndex, uint32_t x, uint32_t z) const;
        void setWeight(uint32_t layerIndex, uint32_t x, uint32_t z, float value);

        // Normalize all layer weights at texel (x, z) so they sum to 1.0
        void normalizeAt(uint32_t x, uint32_t z);
        void normalizeAll();

        // Initialize with default weights: layer 0 = 1.0, rest = 0.0
        void initializeDefault(uint32_t vertexResolution, uint8_t layerCount);

        // Resize to accommodate a new layer count (preserves existing data, renormalizes)
        void setLayerCount(uint8_t newCount);

        // Pack layer weights at texel (x, z) into RGBA for a given texture index
        // textureIndex 0 => layers 0-3, 1 => layers 4-7, etc.
        void packRGBA(uint32_t textureIndex, uint32_t x, uint32_t z,
                      float& r, float& g, float& b, float& a) const;

    };
}
