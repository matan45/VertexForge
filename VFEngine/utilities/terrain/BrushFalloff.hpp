#pragma once

#include "BrushTypes.hpp"

namespace terrain
{
    // Brush falloff curve evaluated on a normalized distance t in [0,1]
    // (0 = brush center, 1 = brush edge). Returns the influence weight in [0,1].
    // This is the single source of truth shared by the terrain brush applicators
    // and the vegetation brush; it mirrors applyFalloff() in the terrain brush shaders
    // (resources/shaders/terrain/brush_compute.glsl, brush_influence.glsl).
    inline float applyFalloff(float t, BrushFalloff falloff)
    {
        switch (falloff)
        {
            case BrushFalloff::Constant: return 1.0f;
            case BrushFalloff::Linear:   return 1.0f - t;
            case BrushFalloff::Smooth:   return 1.0f - t * t * (3.0f - 2.0f * t);
            case BrushFalloff::Sharp:    return 1.0f - t * t;
            default: return 0.0f;
        }
    }
}
