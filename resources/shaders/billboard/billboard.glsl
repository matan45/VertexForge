#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../common/camera_types.glsl"

layout(location = 0) in vec2 inPosition;   // Quad corner offset (-0.5 to 0.5)
layout(location = 1) in vec2 inTexCoord;

layout(location = 2) in vec4 inWorldPosAndAtlas;  // xyz = world position, w = atlas index
layout(location = 3) in vec2 inSize;              // Size in pixels (screen) or world units
layout(location = 4) in uint inSizeMode;          // 0 = ScreenSpace, 1 = WorldSpace
layout(location = 5) in vec4 inColorTint;         // RGBA color tint
layout(location = 6) in vec4 inAnimParams0;       // x=cols y=rows z=frameRate w=spinSpeed
layout(location = 7) in vec4 inAnimParams1;       // x=scrollU y=scrollV z=pulseAmp w=pulseFreq
layout(location = 8) in float inAnimStartTime;    // animation time origin (engine seconds)

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColorTint;

layout(binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    float atlasGridSize;
    float padding;
} pc;

void main() {
    vec3 worldPos = inWorldPosAndAtlas.xyz;
    float atlasIndex = inWorldPosAndAtlas.w;

    vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
    vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);

    // Animation time origin. Defaults (start=0) make t == camera.time.
    float t = camera.time - inAnimStartTime;

    // SPIN: rotate the quad corner offset about the view-plane normal.
    float spinSpeed = inAnimParams0.w;
    vec2 cornerOffset = inPosition;
    if (spinSpeed != 0.0) {
        float a = spinSpeed * t;
        float s = sin(a);
        float c = cos(a);
        cornerOffset = vec2(cornerOffset.x * c - cornerOffset.y * s,
                            cornerOffset.x * s + cornerOffset.y * c);
    }

    // PULSE: throb the effective size. Amplitude 0 -> factor 1 (no change).
    float pulseAmp = inAnimParams1.z;
    float pulseFreq = inAnimParams1.w;
    float pulse = 1.0 + pulseAmp * sin(pulseFreq * t);
    vec2 effSize = inSize * pulse;

    vec3 vertexPos;

    if (inSizeMode == 0u) {
        // Screen-space mode: constant size in pixels regardless of distance

        vec4 centerClip = camera.projection * camera.view * vec4(worldPos, 1.0);

        // cornerOffset is -0.5 to 0.5, so we scale by size/viewportSize
        vec2 ndcOffset = cornerOffset * effSize / pc.viewportSize * 2.0;

        // Apply offset in clip space (multiply by w to maintain proper perspective)
        gl_Position = centerClip;
        gl_Position.xy += ndcOffset * centerClip.w;
    }
    else {
        // World-space mode: size scales with distance (like a physical object)

        vertexPos = worldPos
            + cameraRight * cornerOffset.x * effSize.x
            + cameraUp * cornerOffset.y * effSize.y;

        gl_Position = camera.projection * camera.view * vec4(vertexPos, 1.0);
    }

    float gridSize = pc.atlasGridSize;
    float cols = inAnimParams0.x;
    float rows = inAnimParams0.y;
    float frameRate = inAnimParams0.z;
    bool flipbookActive = (cols * rows > 1.0) && (frameRate > 0.0);

    vec2 uv;
    if (gridSize == 1.0) {
        // Custom/RTT textures: V flip for top-to-bottom storage.
        uv = vec2(inTexCoord.x, 1.0 - inTexCoord.y);
    } else {
        uv = inTexCoord;
    }

    if (flipbookActive) {
        // Flipbook takes precedence (custom/RTT textures, gridSize == 1.0):
        // advance through the sprite sheet at frameRate frames/sec.
        float totalFrames = cols * rows;
        float frame = mod(t * frameRate, totalFrames);
        float col = mod(floor(frame), cols);
        float row = floor(floor(frame) / cols);
        vec2 tileSize = vec2(1.0 / cols, 1.0 / rows);
        fragTexCoord = (vec2(col, row) + uv) * tileSize;
    } else if (gridSize == 1.0) {
        // Custom/RTT textures: sample full (already V-flipped) texture.
        fragTexCoord = uv;
    } else {
        // Editor-icon atlas: map UV to the correct tile within the atlas grid.
        float tileU = mod(atlasIndex, gridSize);
        float tileV = floor(atlasIndex / gridSize);
        vec2 tileSize = vec2(1.0 / gridSize);
        fragTexCoord = (vec2(tileU, tileV) + uv) * tileSize;
    }

    // UV SCROLL: continuous pan (uses absolute camera time, like VFX).
    fragTexCoord += inAnimParams1.xy * camera.time;

    fragColorTint = inColorTint;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColorTint;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D atlasTexture;

void main() {
    vec4 texColor = texture(atlasTexture, fragTexCoord);

    vec4 finalColor = texColor * fragColorTint;

    if (finalColor.a < 0.01) {
        discard;
    }

    outColor = finalColor;
}
