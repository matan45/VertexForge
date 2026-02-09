#pragma once

#include "../../services/providers/ITerrainBrushComputeProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core
{
    class TerrainBrushComputeAdapter : public services::ITerrainBrushComputeProvider
    {
    private:
        controllers::OffScreen& offScreen;

    public:
        explicit TerrainBrushComputeAdapter(controllers::OffScreen& offScreen);
        ~TerrainBrushComputeAdapter() override = default;

        bool applyBrushGPU(
            std::vector<float>& heightData,
            const terrain::BrushGPUParams& params) override;
    };
}
