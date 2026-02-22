#type VERTEX
#version 460 core

// Mesh vertex attributes (binding 0, stride 64)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Instance data (binding 1, per-instance) - locations shifted to avoid conflict with mesh binding
layout(location = 3) in vec4 inWorldPosAndSize;
layout(location = 4) in vec4 inColor;
layout(location = 5) in float inLifetimeRatio;
layout(location = 6) in float inRotation;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;
layout(location = 3) out vec3 fragNormal;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
} camera;

layout(push_constant) uniform PushConstants {
    float alphaClipThreshold;
    uint blendMode;
} pc;

void main() {
    vec3 worldPos = inWorldPosAndSize.xyz;
    float particleSize = inWorldPosAndSize.w;

    // Rotation around Y axis
    float cosR = cos(inRotation);
    float sinR = sin(inRotation);
    mat3 rotY = mat3(
        cosR,  0.0, sinR,
        0.0,   1.0, 0.0,
        -sinR, 0.0, cosR
    );

    vec3 scaledPos = inPosition * particleSize;
    vec3 rotatedPos = rotY * scaledPos;
    vec3 finalPos = worldPos + rotatedPos;

    gl_Position = camera.projection * camera.view * vec4(finalPos, 1.0);

    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragLifetimeRatio = inLifetimeRatio;
    fragNormal = rotY * inNormal;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragLifetimeRatio;
layout(location = 3) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D particleTexture;

layout(push_constant) uniform PushConstants {
    float alphaClipThreshold;
    uint blendMode;
} pc;

void main() {
    vec4 texColor = texture(particleTexture, fragTexCoord);
    vec4 finalColor = texColor * fragColor;

    // Basic directional lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal = normalize(fragNormal);
    float diffuse = max(dot(normal, lightDir), 0.0) * 0.6 + 0.4;
    finalColor.rgb *= diffuse;

    // Lifetime fade
    float fadeStart = 0.8;
    if (fragLifetimeRatio > fadeStart) {
        float fadeProgress = (fragLifetimeRatio - fadeStart) / (1.0 - fadeStart);
        finalColor.a *= 1.0 - smoothstep(0.0, 1.0, fadeProgress);
    }

    if (finalColor.a < pc.alphaClipThreshold) {
        discard;
    }

    if (pc.blendMode == 1u) {
        outColor = vec4(finalColor.rgb * finalColor.a, 0.0);
    } else {
        outColor = finalColor;
    }
}
