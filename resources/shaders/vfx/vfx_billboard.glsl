#type VERTEX
#version 460 core

// Quad vertex (binding 0, vertex rate)
layout(location = 0) in vec2 inPosition;   // Quad corner offset (-0.5 to 0.5)
layout(location = 1) in vec2 inTexCoord;

// Instance data (binding 1, instance rate)
layout(location = 2) in vec4 inWorldPosAndSize;  // xyz = world position, w = size
layout(location = 3) in vec4 inColor;             // RGBA color
layout(location = 4) in float inLifetimeRatio;    // 0 = new, 1 = dying
layout(location = 5) in vec3 inPadding;           // Alignment padding

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
} camera;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    vec2 padding;
} pc;

void main() {
    vec3 worldPos = inWorldPosAndSize.xyz;
    float particleSize = inWorldPosAndSize.w;

    // Extract camera right and up vectors from view matrix for billboarding
    vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
    vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);

    // Calculate billboard vertex position in world space
    vec3 vertexPos = worldPos
        + cameraRight * inPosition.x * particleSize
        + cameraUp * inPosition.y * particleSize;

    gl_Position = camera.projection * camera.view * vec4(vertexPos, 1.0);

    // Pass through to fragment shader
    fragTexCoord = inTexCoord;
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

    // Apply vertex color (includes per-particle tint and alpha from simulation)
    vec4 finalColor = texColor * fragColor;

    // Additional smooth fade-out near end of lifetime
    float fadeStart = 0.8;
    if (fragLifetimeRatio > fadeStart) {
        float fadeProgress = (fragLifetimeRatio - fadeStart) / (1.0 - fadeStart);
        finalColor.a *= 1.0 - smoothstep(0.0, 1.0, fadeProgress);
    }

    // Discard fully transparent pixels
    if (finalColor.a < 0.01) {
        discard;
    }

    outColor = finalColor;
}
