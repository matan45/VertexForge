#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../common/camera_types.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 2) in vec4 inWorldPosAndSize;
layout(location = 3) in vec4 inColor;
layout(location = 4) in float inLifetimeRatio;
layout(location = 5) in float inRotation;
layout(location = 6) in float inFlipbookFrameIndex;
layout(location = 7) in float inGlowIntensity;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;
layout(location = 3) out float fragGlowIntensity;
layout(location = 4) out vec2 fragTexCoordNext;
layout(location = 5) out float fragBlend;

layout(binding = 0) uniform CameraUBO {
    CameraData camera;
};

const uint RENDER_MODE_BILLBOARD = 0u;
const uint RENDER_MODE_HORIZONTAL = 2u;

layout(push_constant) uniform FlipbookPC {
    float flipbookColumns;
    float flipbookRows;
    float alphaClipThreshold;
    uint blendMode;  // 0 = alpha blend, 1 = additive
    uint renderMode;
    float stretchMultiplier;
    float glowColorR;
    float glowColorG;
    float glowColorB;
    float emissiveIntensity;
    float uvScrollSpeedU;
    float uvScrollSpeedV;
    uint frameBlendMode; // VK-1469: 0 = off, 1 = loop (wrap), 2 = clamp (one-shot)
} pc;

void main() {
    vec3 worldPos = inWorldPosAndSize.xyz;
    float particleSize = inWorldPosAndSize.w;

    float cosR = cos(inRotation);
    float sinR = sin(inRotation);
    vec2 rotatedPos = vec2(
        inPosition.x * cosR - inPosition.y * sinR,
        inPosition.x * sinR + inPosition.y * cosR
    );

    vec3 vertexPos;

    if (pc.renderMode == RENDER_MODE_HORIZONTAL) {
        vec3 right = vec3(1.0, 0.0, 0.0);
        vec3 forward = vec3(0.0, 0.0, 1.0);
        vertexPos = worldPos
            + right * rotatedPos.x * particleSize
            + forward * rotatedPos.y * particleSize;
    } else {
        // Standard billboard: camera-facing (also used for stretched in preview since no velocity data)
        vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
        vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);
        vertexPos = worldPos
            + cameraRight * rotatedPos.x * particleSize
            + cameraUp * rotatedPos.y * particleSize;
    }

    gl_Position = camera.projection * camera.view * vec4(vertexPos, 1.0);

    float frameIndex = floor(inFlipbookFrameIndex);
    float col = mod(frameIndex, pc.flipbookColumns);
    float row = floor(frameIndex / pc.flipbookColumns);
    vec2 tileSize = vec2(1.0 / pc.flipbookColumns, 1.0 / pc.flipbookRows);
    fragTexCoord = (vec2(col, row) + inTexCoord) * tileSize;

    fragTexCoord += vec2(pc.uvScrollSpeedU, pc.uvScrollSpeedV) * camera.time;

    // VK-1469: optional crossfade current->next flipbook cell (mirrors vfx_billboard_gpu.glsl).
    // frameBlendMode: 1 = loop (wrap last->first), 2 = clamp (one-shot, hold last).
    float totalFrames = pc.flipbookColumns * pc.flipbookRows;
    if (pc.frameBlendMode != 0u && totalFrames > 1.0) {
        bool loop = (pc.frameBlendMode == 1u);
        float nxt = loop ? mod(frameIndex + 1.0, totalFrames)
                         : min(frameIndex + 1.0, totalFrames - 1.0);
        float ncol = mod(nxt, pc.flipbookColumns);
        float nrow = floor(nxt / pc.flipbookColumns);
        fragTexCoordNext = (vec2(ncol, nrow) + inTexCoord) * tileSize
                         + vec2(pc.uvScrollSpeedU, pc.uvScrollSpeedV) * camera.time;
        fragBlend = fract(inFlipbookFrameIndex);
    } else {
        fragTexCoordNext = fragTexCoord;
        fragBlend = 0.0;
    }

    fragColor = inColor;
    fragLifetimeRatio = inLifetimeRatio;
    fragGlowIntensity = inGlowIntensity;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragLifetimeRatio;
layout(location = 3) in float fragGlowIntensity;
layout(location = 4) in vec2 fragTexCoordNext;
layout(location = 5) in float fragBlend;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D particleTexture;

layout(push_constant) uniform FlipbookPC {
    float flipbookColumns;
    float flipbookRows;
    float alphaClipThreshold;
    uint blendMode;
    uint renderMode;
    float stretchMultiplier;
    float glowColorR;
    float glowColorG;
    float glowColorB;
    float emissiveIntensity;
    float uvScrollSpeedU;
    float uvScrollSpeedV;
    uint frameBlendMode;
} pc;

void main() {
    vec4 texColor = texture(particleTexture, fragTexCoord);
    if (fragBlend > 0.0) {
        texColor = mix(texColor, texture(particleTexture, fragTexCoordNext), fragBlend);
    }
    vec4 finalColor = texColor * fragColor;

    float fadeStart = 0.8;
    if (fragLifetimeRatio > fadeStart) {
        float fadeProgress = (fragLifetimeRatio - fadeStart) / (1.0 - fadeStart);
        finalColor.a *= 1.0 - smoothstep(0.0, 1.0, fadeProgress);
    }

    // Glow: additive emissive color
    vec3 glowColor = vec3(pc.glowColorR, pc.glowColorG, pc.glowColorB);
    finalColor.rgb += glowColor * fragGlowIntensity;
    finalColor.rgb *= pc.emissiveIntensity;

    if (finalColor.a < pc.alphaClipThreshold) {
        discard;
    }

    if (pc.blendMode == 1u) {
        // Additive: premultiplied rgb, zero alpha -> src.rgb + dst
        outColor = vec4(finalColor.rgb * finalColor.a, 0.0);
    } else if (pc.blendMode == 2u) {
        // Premultiplied: straight color + real alpha (fire->smoke gradient)
        outColor = finalColor;
    } else if (pc.blendMode == 3u) {
        // Multiply (dst*src): transparent = white so soft/alpha fade to no-op
        outColor = vec4(mix(vec3(1.0), finalColor.rgb, finalColor.a), finalColor.a);
    } else {
        // Alpha: premultiplied-over (identical result to the legacy straight-alpha path)
        outColor = vec4(finalColor.rgb * finalColor.a, finalColor.a);
    }
}
