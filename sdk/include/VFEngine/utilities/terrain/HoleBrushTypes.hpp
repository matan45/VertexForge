#pragma once

#include "BrushTypes.hpp"
#include <algorithm>

namespace terrain
{
    struct HoleBrushParams
    {
        float radius = 5.0f;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;

        void validate()
        {
            radius = std::max(radius, 0.1f);
        }
    };
}
