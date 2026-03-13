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
}
