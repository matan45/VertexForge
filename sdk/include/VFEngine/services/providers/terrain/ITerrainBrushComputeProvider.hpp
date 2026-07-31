#pragma once

#include <cstdint>
#include <vector>
#include "terrain/BrushTypes.hpp"
#include "terrain/TerrainHydraulicErosion.hpp"

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

        // VK-1616: run the hydraulic erosion solve over ONE gathered region spanning however many
        // tiles the brush touches. Water has to cross tile seams, so unlike applyBrushGPU this is
        // deliberately not per-tile -- the caller gathers a rect in global vertex space and
        // scatters the result back afterwards. `field` is modified in place; `validMask` is 1 per
        // cell a tile owns and 0 for a hole in the grid. Returns true on success.
        virtual bool applyHydraulicErosionGPU(
            std::vector<float>& field,
            const std::vector<uint32_t>& validMask,
            const terrain::HydraulicGPUParams& params) = 0;

        // Upload stamp heightmap data for stamp brush
        virtual void setStampData(
            const std::vector<float>& heights,
            uint32_t width, uint32_t height) = 0;

        virtual void clearStampData() = 0;
    };
}
