#pragma once

#include "GPUVFXTypes.hpp"
#include "../lut/VFXLUTBaker.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>

namespace render::vfx
{
    // VK-1657: the per-instance colour tint (VFXEmitterConfig::colorTint) multiplies every authored
    // colour the GPU sim reads. The renderer rebuilds the GPU config and the LUT from the instance
    // config every frame and tints them on the way to the upload, so a tint never accumulates.
    // ColorBySpeed and the ribbon tail gradient multiply on top of the tinted colour; glow is not
    // tinted. A white tint returns early, so untinted emitters upload byte-identical data.

    // Spawn colour (startColor, also the base of the no-modifier alpha fade) and the
    // colour-over-lifetime endpoints the sim interpolates when no colour LUT is bound.
    inline void applyColorTint(GPUEmitterConfig& config, const glm::vec4& tint)
    {
        if (tint == glm::vec4(1.0f))
            return;

        config.startColor *= tint;
        config.colorStart *= tint;
        config.colorEnd *= tint;
    }

    // The colour-over-lifetime channel of a VFXLUTBaker::bake result (LUTChannel::Color, the first
    // LUT_RESOLUTION entries), only when it is live (LUTFlags::Color) — the sim never samples it
    // otherwise.
    inline void tintLUTColor(LUTBakeResult& lut, const glm::vec4& tint)
    {
        if (tint == glm::vec4(1.0f) || (lut.lutFlags & LUTFlags::Color) == 0u)
            return;

        const size_t begin = static_cast<size_t>(LUTChannel::Color) * GPUVFXConstants::LUT_RESOLUTION;
        const size_t end = std::min(lut.data.size(), begin + GPUVFXConstants::LUT_RESOLUTION);
        for (size_t i = begin; i < end; ++i)
            lut.data[i] *= tint;
    }
}
