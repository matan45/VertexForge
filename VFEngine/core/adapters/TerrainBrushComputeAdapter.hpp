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
            const glm::vec2& brushCenter,
            const glm::vec2& tileWorldOrigin,
            float brushRadius,
            float brushStrength,
            float vertexSpacing,
            uint32_t verticesPerSide,
            terrain::BrushFalloff falloff,
            terrain::BrushShape shape,
            terrain::BrushType brushType,
            float deltaTime,
            float targetHeight,
            float minHeight,
            float maxHeight,
            bool invert) override;
    };
}
