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

#endif // TEXT_SDF_GLSL
