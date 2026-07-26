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

// Lower clamp on the AA half-band, in normalized field units. Only reached where the field
// is saturated (deep inside or outside a glyph, so fwidth == 0); alpha is already 0 or 1
// there, so this merely has to be non-zero - smoothstep(x, x, v) is undefined in GLSL.
const float TEXT_SDF_MIN_AA_WIDTH = 1e-4;

// sdf         - sampled field value, 0..1
// edge        - glyph outline iso-value (FontData::sdfParams.edgeValue, ~0.5)
// smoothWidth - the per-instance sdfParams.y. Only its zero / non-zero state is read:
//               0 marks a non-SDF (GRAYSCALE_8) atlas routed through this branch. That
//               atlas holds coverage, not distance, so it keeps a hard threshold.
// boldBias    - threshold shift for synthetic bold (positive = thicker strokes)
float sdfCoverage(float sdf, float edge, float smoothWidth, float boldBias)
{
    // Take the derivative before any varying-dependent branch.
    float w = max(0.5 * fwidth(sdf), TEXT_SDF_MIN_AA_WIDTH);
    float threshold = edge - boldBias;

    return (smoothWidth > 0.0)
        ? smoothstep(threshold - w, threshold + w, sdf)
        : step(threshold, sdf);
}

#endif // TEXT_SDF_GLSL
