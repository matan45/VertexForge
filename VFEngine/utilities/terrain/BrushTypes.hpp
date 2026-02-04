#pragma once

#include <cstdint>
#include <algorithm>

namespace terrain
{
    enum class BrushType : uint8_t
    {
        Raise = 0,
        Lower = 1,
        Smooth = 2,
        Flatten = 3,
        Noise = 4
    };

    enum class BrushFalloff : uint8_t
    {
        Constant = 0,
        Linear = 1,
        Smooth = 2,
        Sharp = 3
    };

    enum class BrushShape : uint8_t
    {
        Circle = 0,
        Square = 1
    };

    struct BrushParams
    {
        float radius = 5.0f;
        float strength = 0.5f;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            strength = std::clamp(strength, 0.0f, 1.0f);
        }
    };
}
