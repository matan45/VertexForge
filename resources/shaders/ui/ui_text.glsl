#type VERTEX
#version 460 core

// Quad vertices (binding 0, per-vertex)
layout(location = 0) in vec2 inPosition;   // Quad corner offset (-0.5 to 0.5)
layout(location = 1) in vec2 inTexCoord;   // Base UV (0.0 to 1.0)

// Per-instance data (binding 1, per-character)
layout(location = 2) in vec4 inPosAndSize;     // xy = pixel position (anchor), zw = char size in pixels
layout(location = 3) in vec4 inUvRect;         // u0, v0, u1, v1 in font atlas
layout(location = 4) in vec4 inColor;          // RGBA color
layout(location = 5) in vec2 inSdfParams;      // x = sdfEdge, y = sdfSmooth
layout(location = 6) in uint inStyleFlags;     // bit0 = bold, bit1 = italic
// VK-1635 text effects
layout(location = 7) in vec4 inEffectParams;   // outlineWidth, shadowX, shadowY, glowRange (TEXELS)
layout(location = 8) in uvec4 inEffectColors;  // outlineRGBA8, shadowRGBA8, glowRGBA8, flags
layout(location = 9) in float inEffectMargin;  // quad inflation in layout pixels

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec2 fragSdfParams;
layout(location = 3) flat out uint vStyleFlags;
layout(location = 4) flat out vec4 fragUvRect;
layout(location = 5) flat out vec4 fragEffectParams;
layout(location = 6) flat out uvec4 fragEffectColors;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    uint glyphMode;   // 0 = field/coverage, 1 = color bitmap, 2 = MTSDF
    float pxRange;    // VK-1634: SDFParameters::pxRange for glyphMode 2, else 0
} pc;

void main() {
    vec2 charSize = inPosAndSize.zw;

    // Map quad position (-0.5..0.5) to (0..1)
    //
    // VK-1635: then grow it outward by the per-instance effect margin. The glyph quad is
    // exactly its atlas cell, so without this an outline or drop shadow is clipped at the
    // cell border. inEffectMargin is 0 whenever no effect is active, which makes expand 0
    // and leaves localPos bit-identical to the plain inPosition + 0.5. Degenerate cells
    // (whitespace has atlasWidth 0) opt out - there is no area to grow, and dividing by
    // their charSize would be meaningless.
    float margin = (charSize.x > 0.0 && charSize.y > 0.0) ? inEffectMargin : 0.0;
    vec2 expand = margin / max(charSize, vec2(1e-3));
    vec2 localPos = (inPosition + 0.5) * (1.0 + 2.0 * expand) - expand;

    // Interpolate UV within the glyph's atlas rect. On an inflated quad this EXTRAPOLATES
    // past inUvRect; the fragment stage clamps the sample coordinate so it never reads a
    // neighbouring glyph, while the unclamped varying keeps screenPxRange()'s derivative
    // exact (differencing a linear varying is exact wherever it is evaluated).
    fragTexCoord = mix(inUvRect.xy, inUvRect.zw, localPos);

    fragColor = inColor;
    fragSdfParams = inSdfParams;
    vStyleFlags = inStyleFlags;
    fragUvRect = inUvRect;
    fragEffectParams = inEffectParams;
    fragEffectColors = inEffectColors;

    // Screen-space: pixel position + local offset within char
    vec2 pixelPos = inPosAndSize.xy + localPos * inPosAndSize.zw;

    // Italic synthesis: shear top of glyph quad right (tan(12 deg) = 0.2126).
    // Bottom of glyph stays put; SDF/atlas shape unchanged. Skip for color bitmap.
    if ((inStyleFlags & 2u) != 0u && pc.glyphMode != 1u) {
        pixelPos.x += (1.0 - localPos.y) * inPosAndSize.w * 0.2126;
    }

    // Convert to NDC: Vulkan Y goes top(-1) to bottom(+1)
    vec2 ndc = (pixelPos / pc.viewportSize) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
}

#type FRAGMENT
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../common/text_sdf.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec2 fragSdfParams;
layout(location = 3) flat in uint vStyleFlags;
layout(location = 4) flat in vec4 fragUvRect;
layout(location = 5) flat in vec4 fragEffectParams;
layout(location = 6) flat in uvec4 fragEffectColors;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D fontAtlas;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    uint glyphMode;   // 0 = field/coverage, 1 = color bitmap, 2 = MTSDF
    float pxRange;    // VK-1634: SDFParameters::pxRange for glyphMode 2, else 0
} pc;

void main() {
    if (pc.glyphMode == 1u) {
        // Color bitmap mode: sample RGBA directly
        vec4 texColor = texture(fontAtlas, fragTexCoord);
        if (texColor.a < 0.01) {
            discard;
        }
        outColor = vec4(texColor.rgb, texColor.a * fragColor.a);
    } else {
        // Field mode: single-channel SDF/coverage or median-RGB MTSDF.
        // Bold synthesis: bias the threshold so more of the field passes -> thicker strokes.
        //
        // VK-1635: the sample coordinate is clamped to the glyph's own atlas rect because
        // an effect-inflated quad extrapolates fragTexCoord past it, and the atlas packs
        // glyphs edge to edge - one texel outside the rect is a different letter. The
        // clamp is a no-op on an uninflated quad, where mix() already lands inside.
        vec2 atlasSize = vec2(textureSize(fontAtlas, 0));
        vec2 sampleUv = clamp(fragTexCoord, fragUvRect.xy, fragUvRect.zw);
        vec4 fieldSample = texture(fontAtlas, sampleUv);
        float sdfValue = (pc.glyphMode == 2u)
            ? medianRGB(fieldSample.rgb)
            : fieldSample.r;

        float boldBias = ((vStyleFlags & 1u) != 0u) ? 0.15 : 0.0;

        // VK-1634/VK-1635: screen pixels per unit of field, for BOTH encodings - MTSDF
        // reports pxRange directly, legacy SDF_8 recovers spread/edge from sdfParams.
        // Hoisted out of the branches below because the effect path needs it too and
        // everything here runs under pc.glyphMode alone, which is dynamically uniform;
        // fragSdfParams.y is a varying and must never gate a derivative.
        float texelsPerUnit = textTexelsPerFieldUnit(pc.glyphMode, pc.pxRange, fragSdfParams);
        float screenRange = screenPxRange(fragTexCoord, atlasSize, texelsPerUnit);

        if (fragEffectColors.w != 0u) {
            // VK-1635 outline / shadow / glow. The gate is per-instance, but a derivative
            // quad never spans two instances, so it is still quad-uniform - which is what
            // keeps the legacy fwidth() path below well-defined. Nothing inside this
            // branch differentiates anything regardless: textureLod pins the level, and
            // every band comes from the screenRange computed above.
            vec2 shadowUv = clamp(fragTexCoord - fragEffectParams.yz / atlasSize,
                                  fragUvRect.xy, fragUvRect.zw);
            vec4 shadowSample = textureLod(fontAtlas, shadowUv, 0.0);
            float shadowSdf = (pc.glyphMode == 2u)
                ? medianRGB(shadowSample.rgb)
                : shadowSample.r;

            outColor = textEffectComposite(sdfValue, shadowSdf, fragSdfParams.x - boldBias,
                                           screenRange, texelsPerUnit, fragEffectParams,
                                           fragEffectColors, fragColor);
            if (outColor.a < 0.01) {
                discard;
            }
            return;
        }

        float alpha;
        if (pc.glyphMode == 2u) {
            // VK-1634: MTSDF ships a real pxRange, so the band is analytic instead of a
            // derivative of the field. Both .vfFont validators pin pxRange to [1, 16] for
            // any loadable MTSDF font, so the sdfParams.y > 0 guard would be dead weight
            // in this branch.
            alpha = sdfCoverageRange(sdfValue, fragSdfParams.x, boldBias, screenRange);
        } else {
            alpha = (fragSdfParams.y > 0.0)
                ? sdfCoverage(sdfValue, fragSdfParams.x, fragSdfParams.y, boldBias)
                : fieldSample.r;
        }

        if (alpha < 0.01) {
            discard;
        }

        outColor = vec4(fragColor.rgb, fragColor.a * alpha);
    }
}
