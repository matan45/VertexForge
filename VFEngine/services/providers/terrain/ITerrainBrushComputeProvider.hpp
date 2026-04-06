#pragma once

#include <vector>
#include "terrain/BrushTypes.hpp"

namespace services
{
    class ITerrainBrushComputeProvider
    {
    public:
        virtual ~ITerrainBrushComputeProvider() = default;

        // Apply brush on GPU via compute shader.
        // Modifies heightData in-place. Returns true on success.
        virtual bool applyBrushGPU(
            std::vector<float>& heightData,
            const terrain::BrushGPUParams& params) = 0;

        // Upload stamp heightmap data for stamp brush
        virtual void setStampData(
            const std::vector<float>& heights,
            uint32_t width, uint32_t height) = 0;
    };
}
