#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../common/camera_types.glsl"

layout(location = 0) in vec2 inPosition;   // Quad corner offset (-0.5 to 0.5)
layout(location = 1) in vec2 inTexCoord;

// Per-instance data
layout(location = 2) in vec4 inWorldPosAndSize;    // xyz = world position, w = fontSize
layout(location = 3) in vec4 inCharOffsetAndSize;   // xy = charOffset, zw = charSize
layout(location = 4) in vec4 inUvRect;               // u0, v0, u1, v1
layout(location = 5) in vec4 inColor;
layout(location = 6) in uvec2 inRenderModeAndEntity; // x = renderMode, y = entityId
layout(location = 7) in vec2 inSdfParams;            // x = sdfEdge, y = sdfSmooth
layout(location = 8) in uint inStyleFlags;           // bit0 = bold, bit1 = italic

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec2 fragSdfParams;
layout(location = 3) flat out uint vStyleFlags;

layout(binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    uint glyphMode;   // 0 = SDF, 1 = color bitmap
    float padding2;
} pc;

void main() {
    vec3 worldPos = inWorldPosAndSize.xyz;
    vec2 charOffset = inCharOffsetAndSize.xy;
    vec2 charSize = inCharOffsetAndSize.zw;
    uint renderMode = inRenderModeAndEntity.x;

    // Map quad position (−0.5..0.5) to (0..1)
    vec2 localPos = inPosition + 0.5;

    // Interpolate UV within the glyph's atlas rect
    fragTexCoord = mix(inUvRect.xy, inUvRect.zw, localPos);

    fragColor = inColor;
    fragSdfParams = inSdfParams;
    vStyleFlags = inStyleFlags;

    if (renderMode == 0u) {
        // Screen-space mode: position in pixels from top-left
        // worldPos.xy is used as the pixel-space anchor point on screen

        vec2 pixelPos = worldPos.xy + charOffset + localPos * charSize;

        // Italic synthesis: shear top of glyph quad right (tan(12 deg) = 0.2126).
        if ((inStyleFlags & 2u) != 0u && pc.glyphMode == 0u) {
            pixelPos.x += (1.0 - localPos.y) * charSize.y * 0.2126;
        }

        // Convert to NDC: Vulkan Y goes top(-1) to bottom(+1), matching pixel coords
        vec2 ndc = (pixelPos / pc.viewportSize) * 2.0 - 1.0;

        gl_Position = vec4(ndc, 0.0, 1.0);
    }
    else {
        // World-space mode: billboard toward camera

        vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
        vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);

        // Scale factor: convert pixel-space layout to world units
        // Use fontSize as the world-space height reference
        float worldScale = inWorldPosAndSize.w / 32.0;

        vec2 worldOffset = charOffset * worldScale;
        vec2 worldSize = charSize * worldScale;

        vec3 vertexPos = worldPos
            + cameraRight * (worldOffset.x + localPos.x * worldSize.x)
            + cameraUp * (-worldOffset.y - localPos.y * worldSize.y);

        // Italic synthesis: shear top of glyph quad along cameraRight.
        if ((inStyleFlags & 2u) != 0u && pc.glyphMode == 0u) {
            vertexPos += cameraRight * ((1.0 - localPos.y) * worldSize.y * 0.2126);
        }

        gl_Position = camera.projection * camera.view * vec4(vertexPos, 1.0);
    }
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec2 fragSdfParams;
layout(location = 3) flat in uint vStyleFlags;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D fontAtlas;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    uint glyphMode;   // 0 = SDF, 1 = color bitmap
    float padding2;
} pc;

void main() {
    if (pc.glyphMode == 1u) {
        // Color bitmap mode: sample RGBA directly from atlas
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

        float boldBias = ((vStyleFlags & 1u) != 0u) ? 0.15 : 0.0;
        float alpha = smoothstep(edge - smoothWidth - boldBias,
                                 edge + smoothWidth - boldBias, sdfValue);

        if (alpha < 0.01) {
            discard;
        }

        outColor = vec4(fragColor.rgb, fragColor.a * alpha);
    }
}
