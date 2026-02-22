#type VERTEX
#version 460 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 2) in vec4 inWorldPosAndSize;
layout(location = 3) in vec4 inColor;
layout(location = 4) in float inLifetimeRatio;
layout(location = 5) in float inRotation;
layout(location = 6) in float inFlipbookFrameIndex;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
} camera;

// Render mode constants (VK-494)
const uint RENDER_MODE_BILLBOARD = 0u;
const uint RENDER_MODE_STRETCHED = 1u;
const uint RENDER_MODE_HORIZONTAL = 2u;

layout(push_constant) uniform FlipbookPC {
    float flipbookColumns;
    float flipbookRows;
    float alphaClipThreshold;
    uint blendMode;  // 0 = alpha blend, 1 = additive
    uint renderMode;
    float stretchMultiplier;
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
        // Horizontal billboard: flat on XZ plane (VK-494)
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

    fragColor = inColor;
    fragLifetimeRatio = inLifetimeRatio;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragLifetimeRatio;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D particleTexture;

layout(push_constant) uniform FlipbookPC {
    float flipbookColumns;
    float flipbookRows;
    float alphaClipThreshold;
    uint blendMode;
    uint renderMode;
    float stretchMultiplier;
} pc;

void main() {
    vec4 texColor = texture(particleTexture, fragTexCoord);
    vec4 finalColor = texColor * fragColor;

    float fadeStart = 0.8;
    if (fragLifetimeRatio > fadeStart) {
        float fadeProgress = (fragLifetimeRatio - fadeStart) / (1.0 - fadeStart);
        finalColor.a *= 1.0 - smoothstep(0.0, 1.0, fadeProgress);
    }

    if (finalColor.a < pc.alphaClipThreshold) {
        discard;
    }

    if (pc.blendMode == 1u) {
        // Additive: pre-multiply by alpha, output zero alpha
        outColor = vec4(finalColor.rgb * finalColor.a, 0.0);
    } else {
        outColor = finalColor;
    }
}
