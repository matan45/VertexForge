#include "TerrainBrushComputeAdapter.hpp"
#include "../controllers/OffScreen.hpp"

namespace core
{
    TerrainBrushComputeAdapter::TerrainBrushComputeAdapter(controllers::OffScreen& offScreen)
        : offScreen(offScreen)
    {
    }

    bool TerrainBrushComputeAdapter::applyBrushGPU(
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
        bool invert)
    {
        return offScreen.applyBrushGPU(
            heightData, brushCenter, tileWorldOrigin,
            brushRadius, brushStrength, vertexSpacing, verticesPerSide,
            falloff, shape, brushType, deltaTime, targetHeight,
            minHeight, maxHeight, invert);
    }
}
