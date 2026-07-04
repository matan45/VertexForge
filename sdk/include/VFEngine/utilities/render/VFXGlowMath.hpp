#pragma once

namespace render
{
    inline float computeEmissiveGlowScale(float glowLUT, float emissiveIntensity)
    {
        return glowLUT * emissiveIntensity;
    }
}
