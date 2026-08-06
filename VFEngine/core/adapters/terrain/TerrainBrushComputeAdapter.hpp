#pragma once

#include "../../services/providers/terrain/ITerrainBrushComputeProvider.hpp"

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

        bool applyHydraulicErosionGPU(
            std::vector<float>& field,
            const std::vector<uint32_t>& validMask,
            const terrain::HydraulicGPUParams& params) override;

        void setStampData(
            const std::vector<float>& heights,
            uint32_t width, uint32_t height) override;

        void clearStampData() override;
    };
}
