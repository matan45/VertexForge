#pragma once

#include <vector>
#include <glm/glm.hpp>
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
            bool invert) = 0;
    };
}
