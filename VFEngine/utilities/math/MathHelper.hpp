#pragma once
#include <cstdint>
#include <algorithm>

namespace math
{
    // Clamp value between min and max
    template<typename T>
    constexpr T clamp(T value, T minVal, T maxVal)
    {
        return (std::min)((std::max)(value, minVal), maxVal);
    }

    // Smoothstep interpolation (Hermite interpolation)
    // Returns smooth transition from 0 to 1 as x goes from edge0 to edge1
    // Formula: 3t^2 - 2t^3 where t = (x - edge0) / (edge1 - edge0)
    inline float smoothstep(float edge0, float edge1, float x)
    {
        constexpr float epsilon = 1e-6f;
        if (edge1 - edge0 < epsilon)
            return x >= edge0 ? 1.0f : 0.0f;
        float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // Linear interpolation
    template<typename T>
    constexpr T lerp(T a, T b, float t)
    {
        return a + static_cast<T>((b - a) * t);
    }
}

namespace sdf
{
    // Default SDF parameters for font rendering
    constexpr float DEFAULT_EDGE_VALUE = 128.0f;     // 0.5 normalized (edge of glyph)
    constexpr float DEFAULT_SMOOTH_WIDTH = 16.0f;   // Anti-aliasing width in SDF units

    // Convert SDF byte value to alpha using smoothstep
    // sdfValue: raw SDF value (0-255), where 128 = edge
    // edgeCenter: the SDF value representing the glyph edge (typically 128)
    // smoothWidth: width of the anti-aliasing transition zone
    // Returns: alpha value (0.0 - 1.0)
    inline float sdfToAlpha(float sdfValue, float edgeCenter = DEFAULT_EDGE_VALUE,
                            float smoothWidth = DEFAULT_SMOOTH_WIDTH)
    {
        float edge0 = edgeCenter - smoothWidth;
        float edge1 = edgeCenter + smoothWidth;
        return math::smoothstep(edge0, edge1, sdfValue);
    }

    // Convert SDF byte value to alpha byte
    inline uint8_t sdfToAlphaByte(uint8_t sdfValue, float edgeCenter = DEFAULT_EDGE_VALUE,
                                   float smoothWidth = DEFAULT_SMOOTH_WIDTH)
    {
        float alpha = sdfToAlpha(static_cast<float>(sdfValue), edgeCenter, smoothWidth);
        return static_cast<uint8_t>(alpha * 255.0f);
    }
}
