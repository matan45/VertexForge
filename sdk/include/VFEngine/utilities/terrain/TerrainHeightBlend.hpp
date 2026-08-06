#pragma once
#include <algorithm>
#include <cmath>

// VK-1609 terrain height-blended compositing.
//
// The blend itself runs on the GPU, inside the generated terrain composite snippet
// (editor/graph/TerrainCompositeSnippet.hpp -> resources/shaders/material/terrain_material_generated.glsl).
// This header holds the constants that snippet's uploaded scalars are clamped against, and a CPU
// mirror of the exact same arithmetic — kept side by side so the two cannot drift unnoticed, and
// so test_terrain_height_blend.cpp can assert the bit-identity property as an executable check
// rather than a comment.

namespace terrain
{
    // Upper bound on a layer's height-blend contrast. This is a CORRECTNESS invariant, not a
    // taste limit: it keeps exp2's argument inside [-8, 8] (height is [0, 1] and the exponent is
    // contrast * (height - 0.5)), so the sharpening factor stays in [1/256, 256] and is always
    // finite. The linear fallback below multiplies that factor by exactly 0.0, and 0.0 * infinity
    // would be NaN — a finite factor is what makes `contrast == 0 => weight unchanged` hold.
    inline constexpr float MAX_HEIGHT_BLEND_CONTRAST = 16.0f;

    // The generated composite culls a layer at `w < 0.001`. Height sharpening is faded in across
    // [FADE_MIN, FADE_MAX] so it reaches zero exactly at that cull: without the fade, a layer at
    // w just above the cull but with high height would contribute a large share and then vanish,
    // drawing a hard contour ring along the w = 0.001 iso-line.
    inline constexpr float HEIGHT_BLEND_FADE_MIN = 0.001f;
    inline constexpr float HEIGHT_BLEND_FADE_MAX = 0.02f;

    // CPU mirror of the three GLSL lines the composite emits per layer:
    //     float hbAlpha = step(1e-6, hbContrast) * smoothstep(0.001, 0.02, w);
    //     float bw      = w * mix(1.0, exp2(hbContrast * (layerHeight - 0.5)), hbAlpha);
    //
    // Contract, asserted by the tests: with `contrast == 0.0f` this returns `w` with an IDENTICAL
    // bit pattern for every finite w — mix() collapses to exactly 1.0 and `w * 1.0f == w`.
    [[nodiscard]] inline float heightBlendWeight(float w, float height, float contrast) noexcept
    {
        // GLSL step(edge, x): 0.0 when x < edge, else 1.0.
        const float on = (contrast < 1e-6f) ? 0.0f : 1.0f;
        // GLSL smoothstep(e0, e1, x).
        const float t = std::clamp((w - HEIGHT_BLEND_FADE_MIN) / (HEIGHT_BLEND_FADE_MAX - HEIGHT_BLEND_FADE_MIN),
                                   0.0f, 1.0f);
        const float a = on * (t * t * (3.0f - 2.0f * t));
        const float sharpened = std::exp2(contrast * (height - 0.5f));
        // GLSL mix(x, y, a) == x * (1 - a) + y * a.
        return w * (1.0f * (1.0f - a) + sharpened * a);
    }
}
