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
    uint glyphMode;   // 0 = SDF, 1 = color bitmap
    float padding;
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
    if ((inStyleFlags & 2u) != 0u && pc.glyphMode == 0u) {
        pixelPos.x += (1.0 - localPos.y) * inPosAndSize.w * 0.2126;
    }

    // Convert to NDC: Vulkan Y goes top(-1) to bottom(+1)
    vec2 ndc = (pixelPos / pc.viewportSize) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec2 fragSdfParams;
layout(location = 3) flat in uint vStyleFlags;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D fontAtlas;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    uint glyphMode;   // 0 = SDF, 1 = color bitmap
    float padding;
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
        // SDF mode: smoothstep anti-aliasing.
        // Bold synthesis: bias the threshold so more of the field passes -> thicker strokes.
        float sdfValue = texture(fontAtlas, fragTexCoord).r;

        float edge = fragSdfParams.x;
        float smoothWidth = fragSdfParams.y;

        float boldBias = ((vStyleFlags & 1u) != 0u) ? 0.08 : 0.0;
        float alpha = smoothstep(edge - smoothWidth - boldBias,
                                 edge + smoothWidth - boldBias, sdfValue);

        if (alpha < 0.01) {
            discard;
        }

        outColor = vec4(fragColor.rgb, fragColor.a * alpha);
    }
}
