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

layout(push_constant) uniform FlipbookPC {
    float flipbookColumns;
    float flipbookRows;
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

    vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
    vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);

    vec3 vertexPos = worldPos
        + cameraRight * rotatedPos.x * particleSize
        + cameraUp * rotatedPos.y * particleSize;

    gl_Position = camera.projection * camera.view * vec4(vertexPos, 1.0);

    // Flipbook UV remapping (VK-493)
    // Branch-free: when columns=1, rows=1 -> frameIndex=0, col=0, row=0, tileSize=(1,1)
    // so fragTexCoord = inTexCoord (identity)
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

void main() {
    vec4 texColor = texture(particleTexture, fragTexCoord);
    vec4 finalColor = texColor * fragColor;

    float fadeStart = 0.8;
    if (fragLifetimeRatio > fadeStart) {
        float fadeProgress = (fragLifetimeRatio - fadeStart) / (1.0 - fadeStart);
        finalColor.a *= 1.0 - smoothstep(0.0, 1.0, fadeProgress);
    }

    if (finalColor.a < 0.01) {
        discard;
    }

    outColor = finalColor;
}
