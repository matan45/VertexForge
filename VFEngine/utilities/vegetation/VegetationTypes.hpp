#pragma once

#include "../terrain/BrushTypes.hpp"
#include <cstdint>
#include <algorithm>
#include <glm/glm.hpp>

namespace vegetation
{
    enum class DensityBrushType : uint8_t
    {
        Paint = 0,
        Erase = 1,
        Smooth = 2,
        Fill = 3
    };

    enum class PlacementBrushType : uint8_t
    {
        Spread = 0,
        Erase = 1
    };

    struct DensityBrushParams
    {
        float radius = 5.0f;
        float strength = 10.0f;
        float opacity = 1.0f;
        terrain::BrushFalloff falloff = terrain::BrushFalloff::Smooth;
        terrain::BrushShape shape = terrain::BrushShape::Circle;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            strength = std::clamp(strength, 0.0f, 100.0f);
            opacity = std::clamp(opacity, 0.0f, 1.0f);
        }
    };

    struct PlacementBrushParams
    {
        float radius = 10.0f;
        float density = 0.5f;
        float strength = 10.0f;
        float opacity = 1.0f;
        float minScale = 0.8f;
        float maxScale = 1.2f;
        float randomRotation = 1.0f;
        uint32_t speciesId = 0;
        terrain::BrushFalloff falloff = terrain::BrushFalloff::Smooth;
        terrain::BrushShape shape = terrain::BrushShape::Circle;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            density = std::clamp(density, 0.0f, 1.0f);
            strength = std::clamp(strength, 0.0f, 100.0f);
            opacity = std::clamp(opacity, 0.0f, 1.0f);
            minScale = std::max(minScale, 0.01f);
            maxScale = std::max(maxScale, minScale);
            randomRotation = std::clamp(randomRotation, 0.0f, 1.0f);
        }
    };
}
