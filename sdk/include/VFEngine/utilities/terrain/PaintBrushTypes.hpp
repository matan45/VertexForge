#pragma once

#include "BrushTypes.hpp"
#include <cstdint>
#include <algorithm>

namespace terrain
{
    enum class PaintBrushType : uint8_t
    {
        PaintLayer = 0,
        EraseLayer = 1,
        SmoothWeights = 2,
        FillLayer = 3,
        SetBaseLayer = 4
    };

    struct PaintBrushParams
    {
        float radius = 5.0f;
        float strength = 10.0f;
        float opacity = 1.0f;
        uint32_t activeLayer = 0;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            strength = std::clamp(strength, 0.0f, 100.0f);
            opacity = std::clamp(opacity, 0.0f, 1.0f);
        }
    };
}
