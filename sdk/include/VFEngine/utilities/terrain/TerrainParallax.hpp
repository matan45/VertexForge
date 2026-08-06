#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

// VK-1625 terrain parallax from composited height (POM-lite) — CPU mirror of
// resources/shaders/common/terrain_parallax.glsl.
//
// VK-1609 put a real per-layer height in ORM alpha so the composite could sharpen its splat
// weights. This story spends that same height a second time: a view-dependent UV offset applied to
// the ONE base UV every layer derives from (terrain_material_generated.glsl:22-24 —
// `layerUV = triplanarWorldUV * tilingScale`), so all eight layers inherit a world-consistent
// parallax without the generated composite changing by a single token.
//
// This header holds the constants the uploaded scalars are clamped against plus a mirror of the
// exact arithmetic, side by side so the two cannot drift unnoticed and so test_terrain_parallax.cpp
// can assert the identity properties as executable checks rather than comments. Same pattern as
// TerrainAntiTiling.hpp / TerrainWeatherResponse.hpp.
//
// LIVE PATH ONLY, and that is a correctness property rather than a limitation. The RVT bake shader
// is orthographic, top-down and binds no camera, so a view-dependent offset is not merely awkward
// there — it is undefined. The offset is instead applied at FINAL SHADING to both consumers of the
// surface position (the RVT lookup UV and the live composite's base UV), which is why baked pages
// stay view-independent AND resolved/fallback fragments still agree at a page-residency boundary.
// Epic resolves the same conflict the same way, by excluding parallax from the RVT capture.

namespace terrain
{
    // ---------------------------------------------------------------------------------------
    // Authored ranges
    // ---------------------------------------------------------------------------------------

    // Displacement volume depth, in world metres — the artist-meaningful unit, and tiling-independent
    // by construction: the offset is computed in world space and only then projected into UV space,
    // so a layer tiling at 0.1x and one tiling at 10x sink by the same number of centimetres.
    //
    // 0 is the off sentinel, exactly as VK-1611's macroVariationStrength is. It is checked against
    // PARALLAX_AUTHORED_EPS rather than compared to 0.0f so an artist who drags the slider to its
    // floor cannot land on the wrong side of the permutation gate.
    inline constexpr float PARALLAX_MAX_DEPTH = 0.5f;

    // Distance fade, in metres from the camera. VK-1611 deliberately keyed ITS fade on the UV
    // footprint instead, because its effect also runs in the camera-less bake and the two had to
    // agree. Parallax never runs in the bake, so the constraint that forced footprint space does not
    // apply here and the artist-meaningful unit wins. Noted because the divergence is deliberate.
    inline constexpr float PARALLAX_MAX_FADE_DISTANCE = 4096.0f;
    inline constexpr float PARALLAX_DEFAULT_FADE_START = 15.0f;
    inline constexpr float PARALLAX_DEFAULT_FADE_END = 25.0f;
    // smoothstep(e, e, x) is undefined, so the end is always pushed strictly past the start. This is
    // a correctness invariant, not taste — the same class as VK-1611's rescale-width floor.
    inline constexpr float PARALLAX_MIN_FADE_SPAN = 0.05f;

    // Iteration count. Uniform (it comes from a UBO), so the loop stays in uniform control flow and
    // no invocation diverges — the AC's requirement.
    //
    // The ceiling exists because the march costs (steps + 1) height evaluations — the extra one is
    // the entry sample at depth 0, which the first bracket needs — and each evaluation is one ORM
    // textureGrad per ACTIVE splat layer. At 32 steps over 3 active layers that is 99 fetches against
    // a detail-map composite's ~12, so the range is deliberately not open-ended.
    inline constexpr uint32_t PARALLAX_MIN_STEPS = 1u;
    inline constexpr uint32_t PARALLAX_MAX_STEPS = 32u;
    inline constexpr uint32_t PARALLAX_DEFAULT_STEPS = 8u;

    // THE REFERENCE PLANE, and why this knob is not optional.
    //
    // Parallax finds where the view ray crosses a height field whose top plane is h == 1. A field
    // that is CONSTANT at c therefore sits uniformly (1-c)*depth deep, which is a non-zero offset
    // that rotates with the camera — the surface appears to swim. Every POM implementation has this;
    // it cannot be detected without extra samples.
    //
    // It matters here specifically because OrmTexturePacker::DEFAULT_HEIGHT is 128, not 255: an ORM
    // packed after VK-1609 with no height input carries a flat 0.5 alpha. That is invisible to
    // height blending (exp2(k*(0.5-0.5)) == 1) but would swim under parallax. Setting
    // referenceHeight to 0.5 remaps it back to the top plane and the offset returns to exactly zero.
    //
    // Default 1.0 makes the remap the exact identity (see parallaxNormalizeHeight), so every ORM
    // packed BEFORE VK-1609 — alpha force-written to 255, see OrmTexturePacker.cpp:174-179 — already
    // reads as h == 1 and displaces by nothing at all.
    //
    // Clamped away from 0 because the uploaded scalar is the RECIPROCAL and a zero would upload an
    // infinity.
    inline constexpr float PARALLAX_MIN_REFERENCE_HEIGHT = 1.0f / 255.0f;
    inline constexpr float PARALLAX_MAX_REFERENCE_HEIGHT = 1.0f;
    inline constexpr float PARALLAX_DEFAULT_REFERENCE_HEIGHT = 1.0f;

    // A material counts as wanting parallax above this. Mirrors GLSL step(PARALLAX_AUTHORED_EPS, d).
    inline constexpr float PARALLAX_AUTHORED_EPS = 1e-6f;

    // ---------------------------------------------------------------------------------------
    // Grazing-angle handling
    // ---------------------------------------------------------------------------------------
    //
    // The ray offset carries a 1/dot(V,N), which diverges as the view grazes the surface. Two
    // guards, and both are needed:
    //   * the reciprocal is clamped, so the offset can never become unbounded even for one frame;
    //   * the result is ALSO faded to zero across the grazing band, so the effect vanishes smoothly
    //     instead of pinning at the clamp — a hard clamp leaves a visible crease along the iso-line
    //     where it engages, the same failure mode VK-1609's smoothstep exists to avoid.
    //
    // THE INVARIANT BETWEEN THEM, asserted by the tests: PARALLAX_MIN_NDOTV <= PARALLAX_GRAZE_FADE_MIN.
    // The clamp may only engage where the fade has ALREADY reached exactly zero. If the clamp floor
    // sat above the fade's lower edge there would be a band of angles in which the offset is both
    // clamped and still visible, and the crease the fade exists to prevent would be drawn along the
    // clamp's own iso-line instead. Equal is the tightest choice that satisfies it, so the clamp is
    // reached only where the result is multiplied by zero anyway.
    inline constexpr float PARALLAX_MIN_NDOTV = 0.10f;
    inline constexpr float PARALLAX_GRAZE_FADE_MIN = 0.10f;
    inline constexpr float PARALLAX_GRAZE_FADE_MAX = 0.30f;
    static_assert(PARALLAX_MIN_NDOTV <= PARALLAX_GRAZE_FADE_MIN,
                  "the reciprocal clamp must only engage where the grazing fade is already zero");

    // ---------------------------------------------------------------------------------------
    // Authored settings
    // ---------------------------------------------------------------------------------------
    //
    // Material-global, not per-layer, for the same reason VK-1611's anti-tiling is: the offset is
    // applied ONCE to the shared base UV, so a per-layer amplitude is not expressible without either
    // running the composite N times or growing TerrainLayerGPUData past its fully-packed 64 bytes.
    struct TerrainParallaxSettings
    {
        float depthMetres = 0.0f; // 0 = off; the permutation is then not compiled at all
        float fadeStart = PARALLAX_DEFAULT_FADE_START;
        float fadeEnd = PARALLAX_DEFAULT_FADE_END;
        float referenceHeight = PARALLAX_DEFAULT_REFERENCE_HEIGHT;
        uint32_t steps = PARALLAX_DEFAULT_STEPS;

        friend bool operator==(const TerrainParallaxSettings&, const TerrainParallaxSettings&) = default;
    };

    // ---------------------------------------------------------------------------------------
    // Shared scalar helpers
    // ---------------------------------------------------------------------------------------

    // GLSL smoothstep(e0, e1, x). Callers guarantee e1 > e0 (every edge pair here is clamped apart
    // on upload), so the division is safe.
    [[nodiscard]] inline float parallaxSmoothstep(float e0, float e1, float x) noexcept
    {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // 1 inside fadeStart, 0 beyond fadeEnd. Applied to the DEPTH rather than to the finished offset
    // so the search volume shrinks continuously to nothing: at the far edge the marched depth is 0,
    // the solved intersection is 0, and the offset is bitwise zero — which is what lets the whole
    // block sit behind `if (depth > 0.0)` without the branch drawing a seam.
    [[nodiscard]] inline float parallaxDistanceFade(float cameraDistance, float fadeStart, float fadeEnd) noexcept
    {
        return 1.0f - parallaxSmoothstep(fadeStart, fadeEnd, cameraDistance);
    }

    [[nodiscard]] inline float parallaxGrazeFade(float nDotV) noexcept
    {
        return parallaxSmoothstep(PARALLAX_GRAZE_FADE_MIN, PARALLAX_GRAZE_FADE_MAX, nDotV);
    }

    // The effective displacement-volume depth for this fragment. Zero here means the fragment does
    // no work and produces no offset.
    [[nodiscard]] inline float parallaxEffectiveDepth(float depthMetres, float cameraDistance,
                                                      float fadeStart, float fadeEnd, float nDotV) noexcept
    {
        return depthMetres * parallaxDistanceFade(cameraDistance, fadeStart, fadeEnd) * parallaxGrazeFade(nDotV);
    }

    // The reciprocal is taken here, on upload, so the shader multiplies instead of dividing per
    // fragment — the same rule macroVariationFrequency() follows. At the default referenceHeight of
    // 1.0 this returns exactly 1.0f.
    [[nodiscard]] inline float parallaxInvReferenceHeight(float referenceHeight) noexcept
    {
        return 1.0f / std::clamp(referenceHeight, PARALLAX_MIN_REFERENCE_HEIGHT, PARALLAX_MAX_REFERENCE_HEIGHT);
    }

    // Remap a raw ORM alpha onto the [0,1] displacement volume.
    //
    // CONTRACT, asserted by the tests: with invReferenceHeight == 1.0f this is the EXACT identity for
    // every h in [0,1] — `h * 1.0f` is bitwise h, and the clamp cannot move a value already inside
    // its bounds. So the knob's existence cannot perturb a material that never touches it, and a
    // legacy ORM (alpha 255 -> h == 1) still yields a top-plane sample and a zero offset.
    [[nodiscard]] inline float parallaxNormalizeHeight(float ormAlpha, float invReferenceHeight) noexcept
    {
        return std::clamp(ormAlpha * invReferenceHeight, 0.0f, 1.0f);
    }

    // ---------------------------------------------------------------------------------------
    // Geometry — deliberately without a tangent basis
    // ---------------------------------------------------------------------------------------
    //
    // The terrain UV is an AFFINE function of world position (mesh_terrain.glsl:536-550), so a world
    // ray offset maps into UV space exactly, with no TBN and no inverse Jacobian to invert or to
    // keep in sync with the normal-mapping block further down the shader. That is why parallax also
    // works unchanged on cave/triplanar fragments: the map is linear either way.

    // GLSL: `blendWeights = pow(abs(N), vec3(4.0)); blendWeights /= dot(blendWeights, vec3(1.0));`
    //
    // std::pow is used rather than w*w*w*w to mirror the GLSL token for token. GLSL only specs pow to
    // a few ULP, so the last bit may differ from the GPU's — harmless, because these weights are only
    // ever used on BOTH sides of a difference (see parallaxProjectWorldOffset), where any consistent
    // value cancels.
    // The zero-sum fallback has no GLSL counterpart (there the divide would produce NaN); it cannot
    // be reached from a normalized normal and exists only so a test may pass a degenerate vector.
    [[nodiscard]] inline glm::vec3 parallaxTriplanarWeights(const glm::vec3& n) noexcept
    {
        glm::vec3 w{std::pow(std::abs(n.x), 4.0f), std::pow(std::abs(n.y), 4.0f), std::pow(std::abs(n.z), 4.0f)};
        const float sum = w.x + w.y + w.z;
        return (sum > 0.0f) ? w / sum : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // The world-space offset per unit of depth below the surface, where depth is measured ALONG the
    // geometric normal (which is what makes this correct on a slope rather than only on flat ground).
    //
    // V points surface -> eye, so -V goes into the surface. A point z below the surface along the
    // view ray is at P - V * z/dot(V,N): substituting back gives dot(P - Q, N) == z exactly.
    [[nodiscard]] inline glm::vec3 parallaxRayStep(const glm::vec3& v, const glm::vec3& n) noexcept
    {
        return -v / (std::max)(glm::dot(v, n), PARALLAX_MIN_NDOTV);
    }

    // Map a world-space offset into base-UV space.
    //
    // EXACT, not approximate. triplanarWorldUV is `s * (P.xz*w.y + P.xy*w.z + P.yz*w.x)`, which is
    // linear in P for fixed weights, so substituting P + d and subtracting leaves precisely this. The
    // `pureXZ` arm mirrors mesh_terrain.glsl:547, where an RVT build with detail maps drops the
    // triplanar blend on non-cave fragments to match the bake's top-down projection.
    [[nodiscard]] inline glm::vec2 parallaxProjectWorldOffset(const glm::vec3& d,
                                                              const glm::vec3& blendWeights,
                                                              float textureScale,
                                                              bool pureXZ) noexcept
    {
        if (pureXZ)
            return glm::vec2(d.x, d.z) * textureScale;
        return (glm::vec2(d.x, d.z) * blendWeights.y + glm::vec2(d.x, d.y) * blendWeights.z +
                glm::vec2(d.y, d.z) * blendWeights.x) *
               textureScale;
    }

    // ---------------------------------------------------------------------------------------
    // The march
    // ---------------------------------------------------------------------------------------

    struct ParallaxSolve
    {
        float depth = 0.0f;  // solved intersection depth, in metres below the surface
        bool crossed = false; // did the ray meet the height field inside the volume at all
    };

    // Steep-parallax linear search plus one secant refinement, in BRANCHLESS form.
    //
    // The loop runs its full uniform iteration count with no early exit and no `break`: the first
    // crossing is captured arithmetically through `cross`, which is 1.0 exactly once. That is what
    // satisfies the AC's "fixed iteration count (uniform control flow)" — the alternative, breaking
    // out on a per-fragment condition, would put the ORM fetches inside divergent flow and cost more
    // in reconvergence than the skipped iterations save.
    //
    // `sampleHeightAtDepth(z)` returns the composited, reference-remapped height in [0,1] at ray
    // depth z. The caller owns the splat gather; this function owns only the search.
    //
    // CONTRACT, asserted by the tests: for a height field that is CONSTANT at 1 (no ORM anywhere, or
    // any pre-VK-1609 ORM), the surface depth is 0 at every sample, the very first iteration crosses,
    // and the secant weight is exactly 0 -> depth is exactly 0.0f -> the offset is bitwise zero.
    template <typename SampleHeightFn>
    [[nodiscard]] inline ParallaxSolve parallaxSolve(float depthMetres, uint32_t steps,
                                                     SampleHeightFn&& sampleHeightAtDepth)
    {
        const uint32_t n = std::clamp(steps, PARALLAX_MIN_STEPS, PARALLAX_MAX_STEPS);
        const float layerStep = depthMetres / static_cast<float>(n);

        // Step 0 sits on the geometric surface. Its height is the entry sample and is never a
        // crossing candidate on its own — it is only ever the `prev` half of the first bracket.
        float zPrev = 0.0f;
        float sdPrev = depthMetres * (1.0f - sampleHeightAtDepth(0.0f));

        float hitZ0 = 0.0f, hitSd0 = 0.0f, hitZ1 = 0.0f, hitSd1 = 0.0f, found = 0.0f;
        for (uint32_t i = 1u; i <= n; ++i)
        {
            const float z = static_cast<float>(i) * layerStep;
            const float sd = depthMetres * (1.0f - sampleHeightAtDepth(z));
            // GLSL step(sd, z): 1 once the ray has gone at or below the height surface.
            const float below = (z >= sd) ? 1.0f : 0.0f;
            const float cross = below * (1.0f - found);
            hitZ0 += cross * zPrev;
            hitSd0 += cross * sdPrev;
            hitZ1 += cross * z;
            hitSd1 += cross * sd;
            found = (std::max)(found, below);
            zPrev = z;
            sdPrev = sd;
        }

        // Secant between the last two samples: solve (z - sd) == 0 across the bracket. The
        // denominator is (z1 - z0) plus the surface's own change, and it is only degenerate when the
        // bracket is empty, which `found` already covers.
        const float d0 = hitSd0 - hitZ0;
        const float d1 = hitSd1 - hitZ1;
        const float denom = d0 - d1;
        const float w = std::clamp((std::abs(denom) > 1e-8f) ? d0 / denom : 0.0f, 0.0f, 1.0f);
        // Written as GLSL mix() spells it — x*(1-a) + y*a — rather than the algebraically equal
        // a + (b-a)*t, so the two implementations round identically and a future bit-comparison
        // against a GPU capture cannot fail on the form alone.
        const float zStar = hitZ0 * (1.0f - w) + hitZ1 * w;

        // No crossing anywhere in the volume means the ray left through the bottom; the deepest
        // point is the conventional clamp.
        ParallaxSolve out;
        out.crossed = found > 0.0f;
        out.depth = out.crossed ? zStar : depthMetres;
        return out;
    }
}
