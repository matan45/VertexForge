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

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec2 fragSdfParams;
layout(location = 3) flat out uint vStyleFlags;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    uint glyphMode;   // 0 = field/coverage, 1 = color bitmap, 2 = MTSDF
    float pxRange;    // VK-1634: SDFParameters::pxRange for glyphMode 2, else 0
} pc;

void main() {
    // Map quad position (-0.5..0.5) to (0..1)
    vec2 localPos = inPosition + 0.5;

    // Interpolate UV within the glyph's atlas rect
    fragTexCoord = mix(inUvRect.xy, inUvRect.zw, localPos);

    fragColor = inColor;
    fragSdfParams = inSdfParams;
    vStyleFlags = inStyleFlags;

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
        vec4 fieldSample = texture(fontAtlas, fragTexCoord);
        float sdfValue = (pc.glyphMode == 2u)
            ? medianRGB(fieldSample.rgb)
            : fieldSample.r;

        float boldBias = ((vStyleFlags & 1u) != 0u) ? 0.15 : 0.0;

        float alpha;
        if (pc.glyphMode == 2u) {
            // VK-1634: MTSDF ships a real pxRange, so the band is analytic instead of a
            // derivative of the field. glyphMode is a push constant and therefore
            // dynamically uniform, which is what makes the derivatives inside
            // screenPxRange() legal here - fragSdfParams.y is a varying and must not gate
            // them. Both .vfFont validators pin pxRange to [1, 16] for any loadable MTSDF
            // font, so the sdfParams.y > 0 guard would be dead weight in this branch.
            float screenRange = screenPxRange(fragTexCoord,
                                              vec2(textureSize(fontAtlas, 0)),
                                              pc.pxRange);
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
