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
layout(location = 7) in float inGlowIntensity;
// VK-1476: orientation inputs (mirror the runtime GPUParticle fields).
layout(location = 8) in vec3 inVelocity;
layout(location = 9) in uint inSpawnSeed;
layout(location = 10) in float inAge;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;
layout(location = 3) out vec3 fragNormal;
layout(location = 4) out float fragGlowIntensity;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
} camera;

#include "vfx_mesh_orientation.glsl"

layout(push_constant) uniform PushConstants {
    float alphaClipThreshold;
    uint blendMode;
    float glowColorR;
    float glowColorG;
    float glowColorB;
    float emissiveIntensity;
    float uvScrollSpeedU;
    float uvScrollSpeedV;
    // VK-1476: mesh orientation (must match VFXMeshPreviewPushConstants and stay
    // identical in the vertex + fragment blocks).
    uint meshOrientationMode;
    float orientAxisX;
    float orientAxisY;
    float orientAxisZ;
    float meshOrientationSpinRate;
} pc;

void main() {
    vec3 worldPos = inWorldPosAndSize.xyz;
    float particleSize = inWorldPosAndSize.w;

    // VK-1476: shared orientation — matches runtime vfx_mesh_particle.glsl by construction
    // (both include vfx_mesh_orientation.glsl). camBasis columns = world camera right/up/toward-camera.
    mat3 camBasis = transpose(mat3(camera.view));
    vec4 orientParams = vec4(pc.orientAxisX, pc.orientAxisY, pc.orientAxisZ, pc.meshOrientationSpinRate);
    mat3 rot = vfxComputeMeshOrientation(pc.meshOrientationMode, inVelocity, inRotation,
                                         inSpawnSeed, inAge, orientParams, camBasis);

    vec3 scaledPos = inPosition * particleSize;
    vec3 rotatedPos = rot * scaledPos;
    vec3 finalPos = worldPos + rotatedPos;

    gl_Position = camera.projection * camera.view * vec4(finalPos, 1.0);

    fragTexCoord = inTexCoord + vec2(pc.uvScrollSpeedU, pc.uvScrollSpeedV) * camera.time;
    fragColor = inColor;
    fragLifetimeRatio = inLifetimeRatio;
    fragNormal = rot * inNormal;
    fragGlowIntensity = inGlowIntensity;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragLifetimeRatio;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in float fragGlowIntensity;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D particleTexture;

layout(push_constant) uniform PushConstants {
    float alphaClipThreshold;
    uint blendMode;
    float glowColorR;
    float glowColorG;
    float glowColorB;
    float emissiveIntensity;
    float uvScrollSpeedU;
    float uvScrollSpeedV;
    // VK-1476: mesh orientation (must match VFXMeshPreviewPushConstants and stay
    // identical in the vertex + fragment blocks).
    uint meshOrientationMode;
    float orientAxisX;
    float orientAxisY;
    float orientAxisZ;
    float meshOrientationSpinRate;
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
