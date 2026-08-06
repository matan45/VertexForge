#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// VK-1611 terrain anti-tiling, part 1: world-anchored macro variation + distance tiling rescale.
//
// Both effects run on the GPU inside the generated terrain composite snippet
// (editor/graph/TerrainCompositeSnippet.hpp -> resources/shaders/material/terrain_material_generated.glsl),
// which is #include-d verbatim by BOTH the live terrain fragment shader and the RVT bake shader.
// This header holds the constants the uploaded scalars are clamped against plus a CPU mirror of
// the exact same arithmetic, kept side by side so the two cannot drift unnoticed and so
// test_terrain_anti_tiling.cpp can assert the identity properties as executable checks rather
// than comments. Same pattern as TerrainHeightBlend.hpp.
//
// Deliberately dependency-free (<algorithm>/<cmath>/<cstdint> only) so the Editor, Graphics and
// the CPU-only Tests project can all include it.

namespace terrain
{
    // ---------------------------------------------------------------------------------------
    // The distance signal
    // ---------------------------------------------------------------------------------------
    //
    // The RVT bake shader is orthographic, top-down and binds NO camera, so a camera-distance
    // term is not merely awkward there — it is undefined, and it would make every resident page
    // stale the moment the camera moves (TerrainRVTManager's page grid is world-anchored on
    // purpose). The signal is instead the UV FOOTPRINT: log2 of texture repeats per output texel.
    //
    // That quantity is meaningful in both paths. Live it grows with camera distance; in the bake
    // it grows with the page's virtual-texture mip, because a coarser page covers a larger world
    // rect per texel. VT residency picks the page mip so that a page texel is about a screen
    // pixel, so the two agree by construction — which is what makes the baked pages and the live
    // fallback composite the same way at page-residency boundaries.
    //
    // This is also what Epic prescribes for the same problem: "Because RVT shading is
    // camera-independent, this type of shading cannot be expressed directly. However, something
    // similar is achievable by making the shading mip level-dependent" — hence their
    // `Virtual Texture Output Derivative` node.
    //
    // Two deliberate choices in the formula below:
    //   * rho_max (the MAJOR axis), with no division by the anisotropy ratio. Vulkan's lambda_base
    //     divides by eta and therefore tracks the MINOR axis; a grazing RTS view has a hugely
    //     anisotropic footprint and would report "near" for distant ground.
    //   * computed here rather than read from textureQueryLod: the Vulkan spec only BOUNDS rho
    //     (max(|m_ux|,|m_vx|) <= f_x <= sqrt(2)(|m_ux|+|m_vx|)), so hardware LOD is not
    //     reproducible across vendors — disqualifying for content baked into shared pages — and
    //     textureQueryLod is fragment-stage only.
    //
    // GLSL mirror (one line, in uniform control flow in each includer):
    //     float fp = 0.5 * log2(max(dot(dx, dx), dot(dy, dy)));
    [[nodiscard]] inline float footprintLog2(float dxU, float dxV, float dyU, float dyV) noexcept
    {
        const float dxLenSq = dxU * dxU + dxV * dxV;
        const float dyLenSq = dyU * dyU + dyV * dyV;
        // Guard log2(0) at a degenerate derivative (a fully occluded or zero-area quad).
        const float maxLenSq = (std::max)((std::max)(dxLenSq, dyLenSq), 1e-30f);
        return 0.5f * std::log2(maxLenSq);
    }

    // A layer samples at `triplanarWorldUV * tilingScale`, so its footprint is the base footprint
    // shifted by log2(tilingScale) — exact, since tilingScale > 0. This is why ONE material-global
    // knee is correct across layers with different tiling: the knee lives in footprint space, not
    // in world space.
    [[nodiscard]] inline float layerFootprintLog2(float baseFootprintLog2, float tilingScale) noexcept
    {
        return baseFootprintLog2 + std::log2((std::max)(tilingScale, 1e-8f));
    }

    // ---------------------------------------------------------------------------------------
    // Macro variation
    // ---------------------------------------------------------------------------------------

    // Strength is a lerp amount, so 0 must be an exact identity (see macroVariation below).
    inline constexpr float MACRO_VARIATION_MAX_STRENGTH = 1.0f;

    // World size of one noise cycle, in metres. The lower bound is a correctness floor as much as
    // a taste one: the uploaded scalar is the RECIPROCAL, so a size at or below zero would upload
    // an infinity and poison the identity argument below (0.0 * infinity == NaN).
    inline constexpr float MACRO_VARIATION_MIN_SIZE = 1.0f;
    inline constexpr float MACRO_VARIATION_MAX_SIZE = 8192.0f;

    // Defaults follow Godot Terrain3D's macro_variation.glsl, the only implementation that
    // publishes real world scales (UE's macro variation is a community convention built on an
    // Epic-supplied noise TEXTURE, and every tutorial shows its tiling values only as a
    // screenshot). Two octaves at ~250 m and ~132 m, multiplied together.
    inline constexpr float MACRO_VARIATION_DEFAULT_SIZE0 = 250.0f;
    inline constexpr float MACRO_VARIATION_DEFAULT_SIZE1 = 132.0f;

    [[nodiscard]] inline float macroVariationFrequency(float sizeMetres) noexcept
    {
        return 1.0f / std::clamp(sizeMetres, MACRO_VARIATION_MIN_SIZE, MACRO_VARIATION_MAX_SIZE);
    }

    // CPU mirror of the two GLSL lines the composite emits after the layer loop:
    //     float macro = (1.0 + s * (n0 - 0.5)) * (1.0 + s * (n1 - 0.5));
    //     mat_albedo *= macro;
    //
    // Each octave is remapped from [0,1] to a multiplier centred on 1.0, so the product has mean
    // ~1 and the composite's average brightness is preserved. Contract, asserted by the tests:
    // with `strength == 0.0f` this returns EXACTLY 1.0f for every finite n0/n1 — both factors
    // collapse to 1.0f and 1.0f * 1.0f == 1.0f — so `mat_albedo *= macro` cannot perturb a bit.
    //
    // That exactness would have been enough on its own, but the tail is ALSO behind
    // TERRAIN_MACRO_VARIATION: measured with glslc -O against the pre-VK-1611 tree, leaving it
    // ungated cost 4032 bytes of SPIR-V in EVERY permutation (~200 ALU — two octaves of four
    // 32-bit hashes plus the interpolation), which is more than the "identity multiply" framing
    // suggests. Because the emitter places it OUTSIDE the arm chain, that macro costs one extra
    // #ifdef rather than doubling the 16 arms.
    [[nodiscard]] inline float macroVariation(float n0, float n1, float strength) noexcept
    {
        return (1.0f + strength * (n0 - 0.5f)) * (1.0f + strength * (n1 - 0.5f));
    }

    // ---------------------------------------------------------------------------------------
    // Distance tiling rescale
    // ---------------------------------------------------------------------------------------
    //
    // A second albedo tap at a larger world scale, cross-faded in as the footprint grows. This
    // genuinely doubles the albedo fetch count for every layer — MicroSplat's equivalent
    // (Distance Resampling) is documented as taking a worked example from 100 to 196 samples per
    // pixel — which is why it lives behind its own TERRAIN_DISTANCE_RESCALE permutation instead of
    // being data-gated: a material that does not use it does not compile the second textureGrad
    // at all, and its SPIR-V is unchanged.

    inline constexpr float DISTANCE_RESCALE_MAX_STRENGTH = 1.0f;

    // UV multiplier for the far tap. Below 1 the texture reads LARGER (fewer repeats), which is
    // the point; 1.0 would make the far tap identical to the near one and waste the fetch.
    inline constexpr float DISTANCE_RESCALE_MIN_SCALE = 0.05f;
    inline constexpr float DISTANCE_RESCALE_MAX_SCALE = 1.0f;
    inline constexpr float DISTANCE_RESCALE_DEFAULT_SCALE = 0.25f; // texture reads 4x larger

    // The knee and width are in footprint space, i.e. log2(texture repeats per output texel) —
    // resolution-independent, and exactly "how often does the pattern repeat on screen". The
    // default knee of -7 is one repeat per 128 pixels, about where a repeat stops reading as
    // detail and starts reading as a pattern; the fade spans two stops from there.
    inline constexpr float DISTANCE_RESCALE_MIN_KNEE = -16.0f;
    inline constexpr float DISTANCE_RESCALE_MAX_KNEE = 4.0f;
    inline constexpr float DISTANCE_RESCALE_DEFAULT_KNEE = -7.0f;
    inline constexpr float DISTANCE_RESCALE_MIN_WIDTH = 0.25f; // never a step: smoothstep(e,e,x) is undefined
    inline constexpr float DISTANCE_RESCALE_MAX_WIDTH = 8.0f;
    inline constexpr float DISTANCE_RESCALE_DEFAULT_WIDTH = 2.0f;

    // CPU mirror of the blend factor the composite emits:
    //     float t = smoothstep(knee, knee + width, fp) * strength;
    [[nodiscard]] inline float distanceRescaleBlend(float layerFootprintLog2Value,
                                                    float knee,
                                                    float width,
                                                    float strength) noexcept
    {
        // GLSL smoothstep(e0, e1, x). width is clamped away from 0 on upload, so this cannot
        // divide by zero.
        const float t = std::clamp((layerFootprintLog2Value - knee) / width, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t) * strength;
    }
}
