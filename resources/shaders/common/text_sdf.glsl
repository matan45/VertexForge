#ifndef TEXT_SDF_GLSL
#define TEXT_SDF_GLSL

// VK-1631: screen-space anti-aliasing for the single-channel glyph SDF atlas.
// Shared by text/text.glsl (world + screen-space TextComponent) and ui/ui_text.glsl.
//
// The atlas encodes v = edge * (1 + d / spread), with d the signed distance in atlas
// TEXELS, so the field gradient is a constant edge/spread per texel and fwidth(sdf) is
// exactly "field units per screen pixel". Using it as the smoothstep band pins the
// transition to ~1 screen pixel at every text size, replacing the old CPU-side band that
// was fixed in field space (a constant +/-1 atlas texel, i.e. 2 * fontSize / baseFontSize
// screen pixels - too hard when minified, too soft when magnified).
//
// DEPENDS ON: the font atlas sampler being eLinear (TextFontCache.cpp). With eNearest the
// sample is piecewise constant, fwidth(sdf) becomes a spike train and this falls apart.
// CONTROL FLOW: fwidth() is only defined in uniform control flow. Callers reach the SDF
// branch through pc.glyphMode, a push constant, so the branch is dynamically uniform.

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
// smoothWidth - the per-instance sdfParams.y. Callers use zero to identify a native
//               coverage atlas and bypass this distance-field reconstruction.
// boldBias    - threshold shift for synthetic bold (positive = thicker strokes)
float sdfCoverage(float sdf, float edge, float smoothWidth, float boldBias)
{
    // Take the derivative before any varying-dependent branch.
    float w = max(0.5 * fwidth(sdf), TEXT_SDF_MIN_AA_WIDTH);
    float threshold = edge - boldBias;

    return smoothstep(threshold - w, threshold + w, sdf);
}

#endif // TEXT_SDF_GLSL
