#ifndef TEXT_SDF_GLSL
#define TEXT_SDF_GLSL

// Screen-space anti-aliasing for the glyph distance-field atlases.
// Shared by text/text.glsl (world + screen-space TextComponent) and ui/ui_text.glsl.
//
// Two encodings live here, one band each:
//
// VK-1631, glyphMode 0 - the legacy single-channel SDF_8 atlas. It encodes
// v = edge * (1 + d / spread), with d the signed distance in atlas TEXELS, so the field
// gradient is a constant edge/spread per texel and fwidth(sdf) is exactly "field units per
// screen pixel". Using it as the smoothstep band pins the transition to ~1 screen pixel at
// every text size, replacing the older CPU-side band that was fixed in field space (a
// constant +/-1 atlas texel, i.e. 2 * fontSize / baseFontSize screen pixels - too hard when
// minified, too soft when magnified). See sdfCoverage().
//
// VK-1634, glyphMode 2 - the MTSDF_RGBA_32 atlas. Its band is analytic instead: the bake
// stores the pixel range, so the on-screen scale can be derived from the uv varying without
// differencing the field at all. See screenPxRange() and sdfCoverageRange().
//
// DEPENDS ON: the font atlas sampler being eLinear (TextFontCache.cpp). With eNearest the
// sample is piecewise constant, fwidth(sdf) becomes a spike train and this falls apart.
// CONTROL FLOW: derivatives are only defined in uniform control flow. sdfCoverage() is
// reached through the per-instance sdfParams.y, which is per-font and therefore uniform
// across any one draw but not provably so; sdfCoverageRange() is reached through
// pc.glyphMode alone, a push constant, which is.

// Floor on the half-band, in normalized field units: one step of the atlas's R8
// quantisation, since nothing narrower is representable in the field anyway. It does not
// engage at any practical text size (at 200pt off a 32px bake the fwidth term is ~2.5x
// this), so it is a numerical guard rather than a tuning knob: it keeps edge0 < edge1,
// which smoothstep() requires - equal edges are undefined - where the field is saturated
// flat across the 2x2 quad and fwidth collapses to zero.
const float TEXT_SDF_MIN_AA_WIDTH = 1.0 / 255.0;

// VK-1638: procedural missing-glyph tofu. This bit mirrors
// text::STYLE_TOFU and is deliberately separate from bold/italic.
const uint TEXT_STYLE_TOFU = 0x4u;

// Draw a hollow box in quad-local coordinates without sampling the atlas.
// localWidth is computed before the caller's per-instance tofu branch so the
// derivatives remain in uniform control flow.
vec4 textTofuBox(vec2 local, vec4 color, vec2 localWidth)
{
    float aaWidth = max(max(localWidth.x, localWidth.y), 1e-4);
    float stroke = max(0.08, aaWidth * 1.5);
    float edgeDistance =
        min(min(local.x, 1.0 - local.x), min(local.y, 1.0 - local.y));
    float ring = 1.0 - smoothstep(
        stroke - aaWidth,
        stroke + aaWidth,
        edgeDistance);
    return vec4(color.rgb, color.a * ring);
}

// Reconstruct the signed-distance sample from an MSDF/MTSDF RGB triplet.
// The MTSDF alpha channel is intentionally left available for future effects.
float medianRGB(vec3 sampleValue)
{
    return max(min(sampleValue.r, sampleValue.g),
               min(max(sampleValue.r, sampleValue.g), sampleValue.b));
}

// sdf         - sampled field value, 0..1
// edge        - glyph outline iso-value (FontData::sdfParams.edgeValue, ~0.5)
// smoothWidth - the per-instance sdfParams.y. NOT read: the band comes from fwidth(sdf)
//               below. It survives in the signature because callers use zero to identify a
//               native coverage atlas and skip this reconstruction entirely, so the value
//               still has to travel this far.
// boldBias    - threshold shift for synthetic bold (positive = thicker strokes)
float sdfCoverage(float sdf, float edge, float smoothWidth, float boldBias)
{
    // Take the derivative before any varying-dependent branch.
    float w = max(0.5 * fwidth(sdf), TEXT_SDF_MIN_AA_WIDTH);
    float threshold = edge - boldBias;

    return smoothstep(threshold - w, threshold + w, sdf);
}

// Synthetic bold for a NATIVE COVERAGE atlas (grayscale raster, sdfParams == 0). There is
// no distance field to shift a threshold in, so thicken by lifting the coverage curve:
// a monotone gamma that pins 0 -> 0 and 1 -> 1 and pushes every midtone up, which widens
// the antialiased edge ramp the way a heavier face does.
//
// Deliberately derivative-free: callers reach this through a branch on the VARYING
// sdfParams.y, and a derivative under a non-uniform branch is undefined.
float coverageBold(float coverage, float boldBias)
{
    return (boldBias > 0.0) ? pow(coverage, 1.0 - boldBias) : coverage;
}

// VK-1634: floor on the squared per-axis uv gradient. A zero-area quad makes both
// derivatives exactly zero; without this the reciprocal below is +inf, and when pxRange is
// also zero 0 * inf yields a NaN that max() does NOT clamp - GLSL max(NaN, x) returns NaN -
// so it would survive all the way to outColor. Far below any uv step a quad that actually
// covers a pixel can produce, so it never engages in anger.
const float TEXT_SDF_MIN_UV_STEP_SQ = 1e-16;

// VK-1634: screen-space size, in pixels, of one full unit of the MTSDF field.
//
// The importer bakes with msdfgen::Range(pxRange / pxPerEm), and msdfgen ranges are
// SYMMETRIC, so the atlas stores v = 0.5 + d / pxRange with d the signed distance in atlas
// TEXELS. One unit of v therefore spans exactly pxRange texels, and
//     distanceInScreenPixels = (v - 0.5) * screenPxRange().
//
// The derivative is taken on the uv varying, never on the sampled field:
//   * uv is linear in screen space, so differencing it is exact. medianRGB() is only C0
//     across an MSDF channel crease - that is, at every sharp corner - where a 2x2 finite
//     difference of the field is unreliable in both directions: it can spike (the corner
//     blurs away) or collapse (the corner aliases). Sharp corners are the entire point of
//     the encoding.
//   * The field gradient points along the glyph edge normal, so fwidth() of it - an L1
//     stand-in for an L2 magnitude - overstates a 45-degree stroke by up to sqrt(2), about
//     41% of extra blur, and the error varies continuously around a single curve. The uv
//     gradient does not depend on edge orientation at all.
//   * Helper invocations at the quad border evaluate uv extrapolated past the glyph's
//     uvRect, so a field fetch there can land on a neighbouring glyph in the atlas.
//     Differencing a linear varying at an extrapolated position is still exact.
//
// Uses the L2 per-axis magnitude rather than fwidth(uv), which msdfgen documents as an
// approximation, so that sheared quads - italic synthesis shears every one - and the rolled
// quads of world-space text stay correct.
//
// The max(..., 1.0) is msdfgen's: under minification it stops the band collapsing below one
// screen pixel, which is exactly where distance-field text starts to alias.
float screenPxRange(vec2 uv, vec2 atlasSize, float pxRange)
{
    vec2 unitRange = vec2(pxRange) / atlasSize;
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);
    vec2 uvPerPixel = sqrt(max(dx * dx + dy * dy, vec2(TEXT_SDF_MIN_UV_STEP_SQ)));
    vec2 screenTexSize = vec2(1.0) / uvPerPixel;

    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

// VK-1634: MTSDF coverage. Same shape as sdfCoverage() - a smoothstep centred on the
// bold-biased outline iso-value - but the half-band is analytic rather than a derivative of
// the field.
//
// Band: distanceInScreenPixels = (sdf - edge) * screenRange, so a smoothstep spanning 2*w
// field units spans 2*w*screenRange screen pixels. w = 0.5 / screenRange makes that exactly
// one pixel - the same ramp width sdfCoverage() targets for the legacy atlas, so the two
// modes still read as the same typeface renderer.
//
// screenRange - screenPxRange(), already clamped to >= 1, so w never exceeds 0.5. The lower
// clamp engages only past ~128x magnification, where the atlas's own 8-bit quantisation
// would otherwise show as edge banding, and it keeps edge0 < edge1, which smoothstep()
// requires.
float sdfCoverageRange(float sdf, float edge, float boldBias, float screenRange)
{
    float w = max(0.5 / screenRange, TEXT_SDF_MIN_AA_WIDTH);
    float threshold = edge - boldBias;

    return smoothstep(threshold - w, threshold + w, sdf);
}

// ---------------------------------------------------------------------------------------
// VK-1635 - outline, drop shadow and glow.
//
// Everything below evaluates distance BANDS off the same field the base glyph already
// samples: one extra texture fetch (the shadow), no extra descriptors, no extra draw calls.
//
// The whole effect path is analytic. It never differentiates the field, for two reasons
// beyond the ones screenPxRange() already lists: the effect branch is gated on a
// PER-INSTANCE flag word, so it is not dynamically uniform and derivatives taken inside it
// are undefined; and the interesting fragments for a shadow are the ones OUTSIDE the glyph,
// exactly where the field is saturated flat and fwidth() collapses to zero.
//
// Bit positions mirror text::TEXT_EFFECT_* in VFEngine/utilities/text/TextEffects.hpp.
const uint TEXT_EFFECT_OUTLINE = 0x1u;
const uint TEXT_EFFECT_SHADOW = 0x2u;
const uint TEXT_EFFECT_GLOW = 0x4u;

// How many atlas TEXELS one unit of the sampled field spans - the quantity screenPxRange()
// wants as its pxRange argument, generalised to both encodings.
//
//   glyphMode 2 (MTSDF): v = 0.5 + d / pxRange, so one unit of v IS pxRange texels.
//   glyphMode 0 (SDF_8): the importer writes v = edge * (1 + d / spread)
//                        (SDFGenerator.cpp: value = d * onEdge / spread + onEdge), so
//                        dv/dtexel = edge / spread and one unit of v is spread / edge
//                        texels. sdfParams.y is resource::sdfSmoothWidth() = 0.5 / spread,
//                        which is how spread gets recovered here without a new vertex
//                        attribute.
//
// Returns 0 for a coverage atlas (sdfParams.y == 0, i.e. bitmap or colour emoji). Callers
// never reach that: the CPU strips the effect flags for those fonts. The guard is here so
// this stays a total function rather than a division by zero waiting for a caller to
// forget.
//
// sdfParams - the per-instance (edge, smoothWidth) pair, unchanged since VK-1631.
float textTexelsPerFieldUnit(uint glyphMode, float pxRange, vec2 sdfParams)
{
    if (glyphMode == 2u)
    {
        return pxRange;
    }
    if (sdfParams.y <= 0.0 || sdfParams.x <= 0.0)
    {
        return 0.0;
    }
    return (0.5 / sdfParams.y) / sdfParams.x;
}

// Coverage of a band whose edge sits `expand` field units OUTSIDE the glyph outline.
// expand == 0 reproduces the base glyph exactly (the same smoothstep sdfCoverageRange()
// runs), which is what lets the composite below reuse one function for fill, outline and
// shadow rather than keeping three subtly different ramps in sync.
//
// halfBand - 0.5 / screenRange, i.e. a one-screen-pixel ramp, matching every other text path.
float textEffectBand(float sdf, float threshold, float expand, float halfBand)
{
    float d = sdf - threshold + expand;
    return smoothstep(-halfBand, halfBand, d);
}

// Largest `expand` the field can actually represent, in field units.
//
// The field bottoms out at 0, i.e. at `threshold` units outside the outline. Push past that
// and every texel in the saturated region passes the band test at once, so the "outline"
// stops being a ring and becomes the whole quad filled with outline colour - a much worse
// failure than a slightly thin outline. Clamping here degrades instead.
float textEffectMaxExpand(float threshold, float halfBand)
{
    return max(threshold - halfBand, 0.0);
}

// Soft outward falloff for the glow, in field units. 1 at the outline and inside it, fading
// to 0 at `range` units out. Squared rather than linear so the tail dies off smoothly
// instead of ending on a visible band edge.
float textEffectGlow(float sdf, float threshold, float range)
{
    if (range <= 0.0)
    {
        return 0.0;
    }
    float t = clamp(1.0 + (sdf - threshold) / range, 0.0, 1.0);
    return t * t;
}

// Tuck one layer UNDERNEATH what has accumulated so far.
//
// Both arguments are PREMULTIPLIED. This is "dst over src", so callers walk the stack FRONT
// TO BACK: the fill goes in first and stays on top, and each later layer only shows where
// the ones above it left room. Premultiplied is not optional here - straight alpha would
// need a divide per layer and would wash the colour out wherever a layer's alpha is small.
vec4 textEffectUnder(vec4 dst, vec4 src)
{
    return dst + src * (1.0 - dst.a);
}

// Flatten a premultiplied accumulator back to the straight-alpha vec4 the text pipelines'
// blend state expects (srcAlpha / oneMinusSrcAlpha, NOT premultiplied).
vec4 textEffectResolve(vec4 premultiplied)
{
    if (premultiplied.a <= 0.0)
    {
        return vec4(0.0);
    }
    return vec4(premultiplied.rgb / premultiplied.a, premultiplied.a);
}

// The whole effect stack for one fragment.
//
// sdf         - field value at this fragment (median RGB for MTSDF, .r otherwise)
// shadowSdf   - field value at this fragment DISPLACED by the shadow offset. Sampled by the
//               caller because the include must not name the atlas sampler: text.glsl binds
//               it at 1 and ui_text.glsl at 0.
// threshold   - edge - boldBias, the same value the base path uses
// screenRange - screenPxRange(), screen pixels per field unit
// texelsPerUnit - textTexelsPerFieldUnit(); converts the instance's texel distances to
//               field units. Guaranteed > 0 by the caller's flag check.
// params      - (outlineWidth, shadowX, shadowY, glowRange) in atlas texels
// colors      - (outlineRGBA8, shadowRGBA8, glowRGBA8, flags)
// baseColor   - the glyph's own straight-alpha colour; its alpha scales the whole result so
//               a label fade takes the effects with it.
vec4 textEffectComposite(float sdf, float shadowSdf, float threshold, float screenRange,
                         float texelsPerUnit, vec4 params, uvec4 colors, vec4 baseColor)
{
    float halfBand = max(0.5 / screenRange, TEXT_SDF_MIN_AA_WIDTH);
    float maxExpand = textEffectMaxExpand(threshold, halfBand);
    // Guarded rather than a bare reciprocal: a font baked with an on-edge value of 0 makes
    // textTexelsPerFieldUnit() return 0, and 0 * (1/0) is a NaN that max()/min() do NOT
    // clamp - GLSL propagates it - so it would ride all the way out to outColor, where
    // alpha < 0.01 is false for NaN and the fragment never even discards. Zero here just
    // collapses every band onto the glyph edge, which is a visible-but-sane degradation.
    float invUnit = texelsPerUnit > 0.0 ? 1.0 / texelsPerUnit : 0.0;

    float outlineField = 0.0;
    if ((colors.w & TEXT_EFFECT_OUTLINE) != 0u)
    {
        outlineField = min(params.x * invUnit, maxExpand);
    }

    // Front to back - the glyph face first, then whatever shows around it. Depth order is
    // face, outline, glow, shadow: the shadow is the thing cast furthest onto the
    // background, and the glow sits between it and the outline that rims the glyph.
    float fill = textEffectBand(sdf, threshold, 0.0, halfBand);
    vec4 acc = vec4(baseColor.rgb * fill, fill);

    if ((colors.w & TEXT_EFFECT_OUTLINE) != 0u)
    {
        vec4 outlineColor = unpackUnorm4x8(colors.x);
        float coverage = textEffectBand(sdf, threshold, outlineField, halfBand) * outlineColor.a;
        acc = textEffectUnder(acc, vec4(outlineColor.rgb * coverage, coverage));
    }

    if ((colors.w & TEXT_EFFECT_GLOW) != 0u)
    {
        vec4 glowColor = unpackUnorm4x8(colors.z);
        float glowRange = min(params.w * invUnit, maxExpand);
        float coverage = textEffectGlow(sdf, threshold, glowRange) * glowColor.a;
        acc = textEffectUnder(acc, vec4(glowColor.rgb * coverage, coverage));
    }

    if ((colors.w & TEXT_EFFECT_SHADOW) != 0u)
    {
        vec4 shadowColor = unpackUnorm4x8(colors.y);
        // Cast by the OUTLINED silhouette, not the bare glyph, so a thick outline never
        // overhangs its own shadow. buildTextEffectInstance() budgets the quad margin the
        // same way.
        float coverage = textEffectBand(shadowSdf, threshold, outlineField, halfBand) * shadowColor.a;
        acc = textEffectUnder(acc, vec4(shadowColor.rgb * coverage, coverage));
    }

    vec4 resolved = textEffectResolve(acc);
    return vec4(resolved.rgb, resolved.a * baseColor.a);
}

#endif // TEXT_SDF_GLSL
