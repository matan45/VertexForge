#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// VK-1612 terrain hex-tile stochastic sampling — CPU mirror of
// resources/shaders/common/hex_tiling_terrain.glsl. Keep both in sync.
//
// Mikkelsen, "Practical Real-Time Hex-Tiling", JCGT 11(3); weight sharpening per Burley,
// "On Histogram-Preserving Blending for Randomized Texture Tiling", JCGT 8(4), Eq. 5.
//
// Unlike VFEngine/utilities/water/HexTiling.hpp, this twin exists purely so the GPU math is
// unit-testable — nothing on the CPU samples terrain textures. What it buys is the ability to
// assert, as build failures rather than beliefs, the two properties that decide whether this
// feature looks right:
//   * the lattice is EQUILATERAL (the water twin's is not — see the GLSL header comment), so the
//     blend adds no directional bias of its own;
//   * the albedo blend is a CONVEX combination, so it can never overshoot and be clamped into the
//     sRGB8 RVT page.
//
// Deliberately dependency-free (<algorithm>/<array>/<cmath>/<cstdint>) so the Editor, Graphics and
// the CPU-only Tests project can all include it. Same pattern as TerrainHeightBlend.hpp.

namespace terrain
{
    // Burley's exponent. His tuning study: "Ghosting is visible at gamma <= 2, and tile structure
    // is visible at gamma >= 8. Blending with gamma = 4 provides good structure preservation."
    // Mikkelsen hard-codes 7, but he also applies a second gain stage we deliberately do not (it
    // duplicates the job of an exposed exponent).
    inline constexpr float HEX_TILING_DEFAULT_CONTRAST = 4.0f;
    // Below 1 the exponent would FLATTEN the weights instead of sharpening them, reintroducing the
    // ghosting the ramp exists to remove; above 16 the transition band goes sub-pixel and the cell
    // edges alias into a visible "cracked tile" lattice.
    inline constexpr float MIN_HEX_TILING_CONTRAST = 1.0f;
    inline constexpr float MAX_HEX_TILING_CONTRAST = 16.0f;

    // Hex cells per texture repeat.
    inline constexpr float HEX_TILING_DEFAULT_CELL_SCALE = 1.0f;
    inline constexpr float MIN_HEX_TILING_CELL_SCALE = 0.05f;
    inline constexpr float MAX_HEX_TILING_CELL_SCALE = 16.0f;

    // Mikkelsen's own demo ships rotation OFF, so that is the default here too.
    inline constexpr float HEX_TILING_DEFAULT_ROTATION = 0.0f;
    inline constexpr float MAX_HEX_TILING_ROTATION = 1.0f;

    // Mikkelsen g_fallOffContrast — how much the per-tap luminance biases the blend weights.
    inline constexpr float HEX_TILING_FALLOFF_CONTRAST = 0.6f;

    // Lowbias32 (Chris Wellons). Bit-identical in GLSL and C++: every operation is 32-bit unsigned
    // and wraps the same way in both.
    [[nodiscard]] inline uint32_t hexTerrainHashUint(uint32_t x) noexcept
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    struct HexTerrainTap
    {
        float u = 0.0f, v = 0.0f;   // texture UV
        float cos = 1.0f, sin = 0.0f; // this tap's rotation, for un-rotating a tangent-space normal
        float weight = 0.0f;        // Burley-sharpened barycentric weight
        int cellX = 0, cellY = 0;   // lattice index, so tests can reason about which cells were picked
    };

    struct HexTerrainBlend
    {
        std::array<HexTerrainTap, 3> tap{};
    };

    // Cell centre in st (hex) space for a lattice index. invSkew's columns are (1, 0) and
    // (0.5, sqrt(3)/2) — the equilateral basis. Exposed so a test can assert the three pairwise
    // neighbour distances are equal, which is precisely the check the water twin lacks.
    inline void hexTerrainCellCentre(int cx, int cy, float& outX, float& outY) noexcept
    {
        outX = static_cast<float>(cx) + 0.5f * static_cast<float>(cy);
        outY = 0.86602540f * static_cast<float>(cy);
    }

    // Mirror of hexTerrainComputeBlend. Gradients are omitted: they are a pure linear transform of
    // the caller's, and the CPU has nothing to sample with them.
    [[nodiscard]] inline HexTerrainBlend hexTerrainComputeBlend(float u, float v,
                                                                float cellScale,
                                                                float gamma,
                                                                float rotStrength) noexcept
    {
        const float stX = u * cellScale;
        const float stY = v * cellScale;

        // EQUILATERAL skew (Mikkelsen), column-vector convention.
        const float skewX = stX - 0.57735027f * stY;
        const float skewY = 1.15470054f * stY;

        const float baseX = std::floor(skewX);
        const float baseY = std::floor(skewY);
        const float fx = skewX - baseX;
        const float fy = skewY - baseY;
        const float fz = 1.0f - fx - fy;

        // GLSL step(0.0, -fz): 1 when -fz >= 0, i.e. when fz <= 0.
        const float s = (fz <= 0.0f) ? 1.0f : 0.0f;
        const float s2 = 2.0f * s - 1.0f;

        float w[3] = {-fz * s2, s - fy * s2, s - fx * s2};

        const int si = static_cast<int>(s);
        const int bx = static_cast<int>(baseX);
        const int by = static_cast<int>(baseY);
        const int cellX[3] = {bx + si, bx + si, bx + 1 - si};
        const int cellY[3] = {by + si, by + 1 - si, by + si};

        // Burley Eq. 5.
        float total = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            w[i] = std::pow((std::max)(w[i], 0.0f), gamma);
            total += w[i];
        }
        const float invTotal = 1.0f / (std::max)(total, 1e-8f);

        HexTerrainBlend blend;
        for (int i = 0; i < 3; ++i)
        {
            const uint32_t h = hexTerrainHashUint(static_cast<uint32_t>(cellX[i]) * 73856093u
                                                  ^ static_cast<uint32_t>(cellY[i]) * 19349663u);
            const float offU = static_cast<float>(h & 0xFFFFu) * (1.0f / 65535.0f);
            const float offV = static_cast<float>((h >> 16) & 0xFFFFu) * (1.0f / 65535.0f);

            const uint32_t hr = hexTerrainHashUint(h ^ 0x9E3779B9u);
            float rndX = static_cast<float>(hr & 0xFFFFu) * (2.0f / 65535.0f) - 1.0f;
            float rndY = static_cast<float>((hr >> 16) & 0xFFFFu) * (2.0f / 65535.0f) - 1.0f;
            float invLen = 1.0f / std::sqrt((std::max)(rndX * rndX + rndY * rndY, 1e-12f));
            float cs = rndX * invLen;
            float sn = rndY * invLen;

            const float t = std::clamp(rotStrength, 0.0f, 1.0f);
            cs = 1.0f * (1.0f - t) + cs * t;
            sn = 0.0f * (1.0f - t) + sn * t;
            invLen = 1.0f / std::sqrt((std::max)(cs * cs + sn * sn, 1e-12f));
            cs *= invLen;
            sn *= invLen;

            float cenX, cenY;
            hexTerrainCellCentre(cellX[i], cellY[i], cenX, cenY);

            // uv = (R * (st - cen) + cen) / cellScale + off, R = [[c,-s],[s,c]].
            const float px = stX - cenX;
            const float py = stY - cenY;
            const float rx = cs * px - sn * py;
            const float ry = sn * px + cs * py;

            HexTerrainTap& tap = blend.tap[static_cast<size_t>(i)];
            tap.u = (rx + cenX) / cellScale + offU;
            tap.v = (ry + cenY) / cellScale + offV;
            tap.cos = cs;
            tap.sin = sn;
            tap.weight = w[i] * invTotal;
            tap.cellX = cellX[i];
            tap.cellY = cellY[i];
        }
        return blend;
    }

    // Mirror of hexTerrainSampleAlbedo's combine step, taking the three already-fetched taps.
    // Contract, asserted by the tests: the returned value lies within [min(c), max(c)] for every
    // channel, because the final weights are non-negative and sum to 1. That convexity is what
    // distinguishes this from the variance-preserving blend the ocean uses, which would overshoot
    // and be clamped by the sRGB8 RVT page.
    inline void hexTerrainCombineAlbedo(const HexTerrainBlend& blend,
                                        const float c0[3], const float c1[3], const float c2[3],
                                        float out[3]) noexcept
    {
        constexpr float LwR = 0.299f, LwG = 0.587f, LwB = 0.114f;
        auto luma = [](const float* c) { return LwR * c[0] + LwG * c[1] + LwB * c[2]; };

        const float f = HEX_TILING_FALLOFF_CONTRAST;
        const float d0 = 1.0f * (1.0f - f) + luma(c0) * f;
        const float d1 = 1.0f * (1.0f - f) + luma(c1) * f;
        const float d2 = 1.0f * (1.0f - f) + luma(c2) * f;

        float W0 = d0 * blend.tap[0].weight;
        float W1 = d1 * blend.tap[1].weight;
        float W2 = d2 * blend.tap[2].weight;
        const float inv = 1.0f / (std::max)(W0 + W1 + W2, 1e-8f);
        W0 *= inv;
        W1 *= inv;
        W2 *= inv;

        for (int ch = 0; ch < 3; ++ch)
            out[ch] = W0 * c0[ch] + W1 * c1[ch] + W2 * c2[ch];
    }

    // Mirror of the per-tap normal correction: a tap fetched at a UV rotated by R must have its
    // tangent-space xy rotated by R TRANSPOSE to land back in the surface's tangent frame.
    // Derivation is spelled out in the GLSL; the test round-trips it against a known gradient.
    inline void hexTerrainUnrotateNormal(float cos, float sin, const float n[3], float out[3]) noexcept
    {
        out[0] = cos * n[0] + sin * n[1];
        out[1] = -sin * n[0] + cos * n[1];
        out[2] = n[2];
    }
}
