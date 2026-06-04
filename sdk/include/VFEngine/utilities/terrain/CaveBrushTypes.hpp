#pragma once

#include "BrushTypes.hpp"
#include <cstdint>
#include <algorithm>

namespace terrain
{
    enum class CaveBrushType : uint8_t
    {
        Carve = 0,  // Subtract from volume (create cave space)
        Fill = 1,   // Add to volume (restore solid)
        Smooth = 2  // Smooth cave surfaces (reduce staircase artifacts)
    };

    struct CaveBrushParams
    {
        float radius = 5.0f;
        float strength = 10.0f;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            strength = std::clamp(strength, 0.0f, 100.0f);
        }
    };
}
