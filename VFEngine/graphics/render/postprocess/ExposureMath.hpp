#pragma once

#include <cfloat>

namespace render::postprocess
{
    // VK-1663: a LINEAR exposure multiplier that is safe to hand on: the DLSS exposure texel (DLSS-SR reads it with
    // useAutoExposure = eFalse) and the auto-exposure readback. NaN, +/-Inf, zero, negative and subnormal values become
    // the neutral 1. Streamline forwards the texel to DLSS unchecked, and a bad value would persist in its history.
    // Comparisons with NaN are false under the project's default /fp:precise; the lower bound is FLT_MIN rather than 0
    // because 1/subnormal overflows to Inf.
    [[nodiscard]] constexpr float sanitizeExposure(float exposure) noexcept
    {
        return exposure >= FLT_MIN && exposure <= FLT_MAX ? exposure : 1.0f;
    }
}
