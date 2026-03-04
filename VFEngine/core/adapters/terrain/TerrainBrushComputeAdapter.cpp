#include "TerrainBrushComputeAdapter.hpp"
#include "../../controllers/OffScreen.hpp"

namespace core
{
    TerrainBrushComputeAdapter::TerrainBrushComputeAdapter(controllers::OffScreen& offScreen)
        : offScreen(offScreen)
    {
    }

    bool TerrainBrushComputeAdapter::applyBrushGPU(
        std::vector<float>& heightData,
        const terrain::BrushGPUParams& params)
    {
        return offScreen.applyBrushGPU(heightData, params);
    }
}
