#pragma once

namespace vfx
{
    // VK-1473: remap a raw scalar (e.g. particle speed = length(velocity)) into [0,1] over
    // [lo, hi]. Clamps at 0 (value <= lo) and 1 (value >= hi); a degenerate range (hi <= lo)
    // maps to 0. Mirrors the GLSL helper vfxNormalize01 in vfx_lut.glsl.
    inline float normalizedSpeed01(float value, float lo, float hi)
    {
        if (hi - lo <= 1e-6f)
            return 0.0f;
        const float t = (value - lo) / (hi - lo);
        if (t < 0.0f)
            return 0.0f;
        if (t > 1.0f)
            return 1.0f;
        return t;
    }
}
