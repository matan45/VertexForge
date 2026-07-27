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

    // VK-1635: pack a linear RGBA colour into one uint32_t, 8 bits per channel.
    //
    // Byte order matches GLSL's unpackUnorm4x8(): red occupies the LEAST significant
    // byte, alpha the most. Getting this backwards is silent - the colour simply comes
    // out wrong in the shader - so the pair below is round-trip tested.
    //
    // Deliberately takes loose floats rather than a glm::vec4: this header is included
    // almost everywhere and pulling glm in here would be a large compile-time tax for
    // two functions. Callers with a vec4 pass .r/.g/.b/.a.
    inline uint32_t packRGBA8(float r, float g, float b, float a)
    {
        auto channel = [](float v) -> uint32_t
        {
            // +0.5 then truncate: round-to-nearest, so 1.0 lands exactly on 255 and the
            // unpack round-trip is exact for every byte value.
            return static_cast<uint32_t>(clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        };

        return channel(r) | (channel(g) << 8) | (channel(b) << 16) | (channel(a) << 24);
    }

    // Inverse of packRGBA8. Writes into four loose floats for the same reason.
    inline void unpackRGBA8(uint32_t packed, float& r, float& g, float& b, float& a)
    {
        constexpr float inv255 = 1.0f / 255.0f;
        r = static_cast<float>(packed & 0xFFu) * inv255;
        g = static_cast<float>((packed >> 8) & 0xFFu) * inv255;
        b = static_cast<float>((packed >> 16) & 0xFFu) * inv255;
        a = static_cast<float>((packed >> 24) & 0xFFu) * inv255;
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

    // VK-1634: screen-space size, in pixels, of one full unit of an MTSDF field.
    //
    // Mirrors screenPxRange() in resources/shaders/common/text_sdf.glsl - keep the two in
    // step. An MTSDF atlas stores v = 0.5 + d / pxRange with d the signed distance in atlas
    // TEXELS, so one unit of v spans pxRange texels; scaling by the on-screen magnification
    // converts that to screen pixels. The shader recovers the magnification from the uv
    // derivatives; callers here pass it directly.
    //
    // Never below 1: under that the field can no longer carry an anti-aliased edge and the
    // band has to stay a full pixel wide. Also makes a zero pxRange degrade softly instead
    // of dividing by zero in mtsdfHalfBand().
    inline float mtsdfScreenPxRange(float pxRange, float screenPixelsPerAtlasTexel)
    {
        return (std::max)(pxRange * screenPixelsPerAtlasTexel, 1.0f);
    }

    // VK-1634: half-width of the MTSDF anti-aliasing band, in normalized field units.
    // A smoothstep over [edge - w, edge + w] ramps across 2*w field units, i.e.
    // 2*w*screenPxRange screen pixels; w = 0.5 / screenPxRange makes that exactly one pixel.
    inline float mtsdfHalfBand(float screenPxRange)
    {
        return 0.5f / (std::max)(screenPxRange, 1.0f);
    }
}
