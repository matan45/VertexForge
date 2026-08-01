#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// VK-1620 mesh-into-terrain blending — the blend curve, and the packing that carries its two
// parameters to the GPU.
//
// UE5's killer RVT feature: a rock or cliff samples the terrain's runtime virtual texture at its
// own world XZ and fades its surface toward the terrain's within a height band above the ground, so
// the prop melts into the terrain instead of meeting it at a hard contact line.
//
// This header exists for the same reason TerrainHeightBlend.hpp does: the curve is evaluated in
// mesh_shader_gpudriven.glsl, and the ONLY way to keep a CPU-side claim about it honest is to write
// the arithmetic once, here, and have a test assert the shader's copy against it. Header-only and
// dependency-free so Utilities, Graphics, the Editor and the CPU-only Tests project all share one
// definition.

namespace material
{
    // The band is a distance in WORLD METRES above the terrain surface. The floor is not 0: a zero
    // band would make the blend a step function at the contact line, which is the artefact the
    // feature exists to remove. The ceiling is generous — a cliff base can legitimately want metres.
    inline constexpr float MIN_TERRAIN_BLEND_BAND = 0.01f;
    inline constexpr float MAX_TERRAIN_BLEND_BAND = 16.0f;
    inline constexpr float DEFAULT_TERRAIN_BLEND_BAND = 0.35f;

    // Shapes the falloff across the band. 1 = the raw smoothstep; higher pulls the blend down
    // toward the ground so only the last few centimetres take terrain colour.
    //
    // The floor is 1, NOT 0, and that is load-bearing: pow(x, 0) is 1 for every x, which would
    // blend the ENTIRE band at full strength and then cut hard at the band's top edge — the exact
    // opposite of what the control reads as. Clamping into [MIN, MAX] rather than to 0 is the same
    // convention TerrainMaterialTypes.hpp uses for its per-layer opt-in scalars.
    inline constexpr float MIN_TERRAIN_BLEND_CONTRAST = 1.0f;
    inline constexpr float MAX_TERRAIN_BLEND_CONTRAST = 8.0f;
    inline constexpr float DEFAULT_TERRAIN_BLEND_CONTRAST = 2.0f;

    [[nodiscard]] inline constexpr float clampTerrainBlendBand(float band) noexcept
    {
        if (band < MIN_TERRAIN_BLEND_BAND) return MIN_TERRAIN_BLEND_BAND;
        if (band > MAX_TERRAIN_BLEND_BAND) return MAX_TERRAIN_BLEND_BAND;
        return band;
    }

    [[nodiscard]] inline constexpr float clampTerrainBlendContrast(float contrast) noexcept
    {
        if (contrast < MIN_TERRAIN_BLEND_CONTRAST) return MIN_TERRAIN_BLEND_CONTRAST;
        if (contrast > MAX_TERRAIN_BLEND_CONTRAST) return MAX_TERRAIN_BLEND_CONTRAST;
        return contrast;
    }

    // How much of the terrain to mix in at `heightAboveTerrain` metres above the terrain surface.
    //
    // MUST stay equivalent to terrainBlendAlpha() in mesh_shader_gpudriven.glsl.
    //   d <= 0      -> 1  (at or below the surface: fully terrain — this falls out of the clamped
    //                      smoothstep, it is not a special case and there is no branch for it)
    //   d >= band   -> 0  (clear of the band: the prop's own material, untouched)
    //   in between  -> pow(1 - smoothstep(0, band, d), contrast), monotonically decreasing
    [[nodiscard]] inline float terrainBlendAlpha(float heightAboveTerrain, float band, float contrast) noexcept
    {
        band = clampTerrainBlendBand(band);
        contrast = clampTerrainBlendContrast(contrast);

        // smoothstep(0, band, d), spelled out rather than delegated so this and the GLSL cannot
        // drift apart through a library difference.
        float t = heightAboveTerrain / band;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        const float s = t * t * (3.0f - 2.0f * t);

        const float a = 1.0f - s;
        // Both ends are exact: 0^c == 0 and 1^c == 1 for the c in [1, 8] the clamp guarantees.
        return std::pow(a, contrast);
    }

    // --- GPU packing -------------------------------------------------------------------------
    // Band and contrast ride PerDrawData.instanceData.z as two halfs, because that uvec4 component
    // is genuinely unread (only .w is, by the shadow and task shaders) and PerDrawData is exactly
    // 256 bytes with no padding — growing it would cost 6.25% of a buffer sized for 700k draws.
    //
    // Mirrors GLSL packHalf2x16/unpackHalf2x16: x in the low 16 bits, y in the high 16.
    // IEEE 754 binary16, round-to-nearest-even, which is what both the GPU and this do.
    [[nodiscard]] inline uint16_t floatToHalfBits(float value) noexcept
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));

        const uint32_t sign = (bits >> 16) & 0x8000u;
        int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
        uint32_t mantissa = bits & 0x7FFFFFu;

        if (exponent >= 0x1F)                       // overflow -> inf
            return static_cast<uint16_t>(sign | 0x7C00u);
        if (exponent <= 0)                          // underflow -> zero (subnormals not needed:
            return static_cast<uint16_t>(sign);     // the clamped ranges never reach them)

        // Round to nearest even on the 13 bits being discarded.
        const uint32_t roundBit = 0x1000u;
        if ((mantissa & roundBit) != 0u &&
            ((mantissa & (roundBit - 1u)) != 0u || (mantissa & (roundBit << 1)) != 0u))
        {
            mantissa += roundBit;
            if (mantissa > 0x7FFFFFu) { mantissa = 0; ++exponent; }
            if (exponent >= 0x1F) return static_cast<uint16_t>(sign | 0x7C00u);
        }
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
    }

    [[nodiscard]] inline float halfBitsToFloat(uint16_t half) noexcept
    {
        const uint32_t sign = static_cast<uint32_t>(half & 0x8000u) << 16;
        const uint32_t exponent = (half >> 10) & 0x1Fu;
        const uint32_t mantissa = half & 0x3FFu;

        uint32_t bits;
        if (exponent == 0)
            bits = sign; // zero (subnormals unreachable from the clamped ranges)
        else if (exponent == 0x1F)
            bits = sign | 0x7F800000u | (mantissa << 13);
        else
            bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);

        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    // Clamps INTO the valid ranges before packing, so an out-of-range authored value can never
    // reach the shader — the shader trusts these and does not re-clamp.
    [[nodiscard]] inline uint32_t packTerrainBlendParams(float band, float contrast) noexcept
    {
        const uint32_t lo = floatToHalfBits(clampTerrainBlendBand(band));
        const uint32_t hi = floatToHalfBits(clampTerrainBlendContrast(contrast));
        return lo | (hi << 16);
    }

    inline void unpackTerrainBlendParams(uint32_t packed, float& band, float& contrast) noexcept
    {
        band = halfBitsToFloat(static_cast<uint16_t>(packed & 0xFFFFu));
        contrast = halfBitsToFloat(static_cast<uint16_t>(packed >> 16));
    }
}
