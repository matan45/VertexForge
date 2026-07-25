#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

namespace water
{
    // VK-1604: CPU mirror of resources/shaders/water/hex_tiling.glsl — hex tile-and-blend
    // anti-tiling for the periodic FFT displacement/normal maps (Ubisoft La Forge, "Making Waves
    // in Ocean Surface Rendering using Tiling and Blending"; lattice per Mikkelsen, "Practical
    // Real-Time Hex-Tiling", JCGT 11(3)).
    //
    // Keep both files in sync. The hash is INTEGER, not fract(sin(...)): GPU sin() is neither
    // IEEE-exact nor consistent across vendors, and fract(sin(x)) degenerates for large x — and
    // ocean UVs are world-metres/patchSize, which grows without bound as the camera travels.
    // Integer ops are exact and identical on both sides, so the same cell always picks the same
    // offset on the CPU and the GPU.
    //
    // This exists because CPU buoyancy samples the same maps: without mirroring the blend, the
    // rendered surface and the physics surface would disagree by up to the wave amplitude of
    // whichever bands are tiled. Full bit-equality is NOT achievable (the GPU filters with
    // fixed-point subtexel weights while the CPU uses float32) and is not the goal — structural
    // agreement well inside wave amplitude is.

    inline constexpr uint32_t HEX_BAND_COUNT = 3;

    // Lowbias32 integer hash (Chris Wellons). Bit-identical in GLSL and C++.
    inline uint32_t hexHashUint(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    // Deterministic per-cell UV offset in [0,1). The texture repeats, so any translation is a
    // valid re-tiling of the same exemplar.
    inline glm::vec2 hexCellOffset(int cellX, int cellY)
    {
        const uint32_t hx = static_cast<uint32_t>(cellX) * 73856093u;
        const uint32_t hy = static_cast<uint32_t>(cellY) * 19349663u;
        const uint32_t h = hexHashUint(hx ^ hy);
        return glm::vec2(static_cast<float>(h & 0xFFFFu) / 65535.0f,
                         static_cast<float>((h >> 16) & 0xFFFFu) / 65535.0f);
    }

    // The three exemplar lookups a texel blends between, plus their weights.
    struct HexBlend
    {
        glm::vec2 uv[3]{};      // already offset — sample the band map at these
        float weight[3]{};      // contrast-sharpened, sum == 1
        // Multiplier that restores variance after the weighted sum. A plain weighted mean of
        // three independent samples shrinks variance (by up to 1/sqrt(3) at a triangle centre),
        // which visibly FLATTENS the waves — unacceptable for a displacement map that also
        // drives buoyancy. Apply to zero-mean channels (displacement xyz) only; foam and
        // normals must use the plain weighted mean, or non-negative foam can go negative.
        float varianceScale = 1.0f;
    };

    // uv is in patch space (worldPos.xz / patchSize). cellScale = hex cells per patch.
    inline HexBlend hexComputeBlend(const glm::vec2& uv, float cellScale, float contrast)
    {
        HexBlend blend;

        const glm::vec2 scaledUV = uv * cellScale;

        // Skew into a triangular lattice: columns (1, -1/sqrt(3)) and (0, 2/sqrt(3)).
        const glm::vec2 skewed(scaledUV.x,
                               -0.57735027f * scaledUV.x + 1.15470054f * scaledUV.y);

        const glm::vec2 baseCell(std::floor(skewed.x), std::floor(skewed.y));
        const int baseX = static_cast<int>(baseCell.x);
        const int baseY = static_cast<int>(baseCell.y);

        const float fx = skewed.x - baseCell.x;
        const float fy = skewed.y - baseCell.y;
        const float fz = 1.0f - fx - fy;

        // Which of the two triangles in this rhombus contains the point.
        const float s = (fz <= 0.0f) ? 1.0f : 0.0f;
        const float s2 = 2.0f * s - 1.0f;

        float w[3];
        w[0] = -fz * s2;
        w[1] = s - fy * s2;
        w[2] = s - fx * s2;

        const int si = static_cast<int>(s);
        const int cellX[3] = {baseX + si, baseX + si, baseX + 1 - si};
        const int cellY[3] = {baseY + si, baseY + 1 - si, baseY + si};

        // Sharpen then renormalize. max(w, 0) guards pow() against tiny negative fp noise,
        // which would otherwise produce NaN.
        float total = 0.0f;
        for (uint32_t i = 0; i < 3; ++i)
        {
            w[i] = std::pow(glm::max(w[i], 0.0f), contrast);
            total += w[i];
        }
        const float invTotal = 1.0f / glm::max(total, 1e-8f);

        float sumSq = 0.0f;
        for (uint32_t i = 0; i < 3; ++i)
        {
            w[i] *= invTotal;
            sumSq += w[i] * w[i];
            blend.weight[i] = w[i];
            blend.uv[i] = uv + hexCellOffset(cellX[i], cellY[i]);
        }

        blend.varianceScale = 1.0f / std::sqrt(glm::max(sumSq, 1e-8f));
        return blend;
    }

    // Variance-preserving combine — for zero-mean channels (FFT displacement has no DC term).
    inline float hexCombineVariancePreserving(const HexBlend& blend, float s0, float s1, float s2)
    {
        return (blend.weight[0] * s0 + blend.weight[1] * s1 + blend.weight[2] * s2)
             * blend.varianceScale;
    }

    // Mean-preserving combine — for foam and normals (non-negative / directional).
    inline float hexCombineMeanPreserving(const HexBlend& blend, float s0, float s1, float s2)
    {
        return blend.weight[0] * s0 + blend.weight[1] * s1 + blend.weight[2] * s2;
    }

    inline bool hexBandEnabled(uint32_t bandMask, uint32_t band)
    {
        return (bandMask & (1u << band)) != 0u;
    }
}
