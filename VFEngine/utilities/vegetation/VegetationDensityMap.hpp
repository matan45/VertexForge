#pragma once

#include <vector>
#include <cstdint>

namespace vegetation
{
    struct VegetationDensityMap
    {
        // Flat vector of size resolution*resolution
        // Values in [0.0, 1.0] representing grass density at each texel
        std::vector<float> densityData;
        uint32_t resolution = 0; // matches tile vertex count (33, 65, 129)

        [[nodiscard]] bool isInitialized() const { return resolution > 0 && !densityData.empty(); }
        [[nodiscard]] size_t getTexelCount() const { return static_cast<size_t>(resolution) * resolution; }

        [[nodiscard]] float getDensity(uint32_t x, uint32_t z) const;
        void setDensity(uint32_t x, uint32_t z, float value);

        // Bilinear interpolation for smooth sampling between texels
        [[nodiscard]] float sampleBilinear(float u, float v) const;

        // Initialize all densities to 0.0 (no grass)
        void initializeDefault(uint32_t vertexResolution);

        // Clear all data
        void clear();
    };
}
