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

    void TerrainBrushComputeAdapter::setStampData(
        const std::vector<float>& heights,
        uint32_t width, uint32_t height)
    {
        offScreen.setStampData(heights, width, height);
    }

    void TerrainBrushComputeAdapter::clearStampData()
    {
        offScreen.clearStampData();
    }
}
