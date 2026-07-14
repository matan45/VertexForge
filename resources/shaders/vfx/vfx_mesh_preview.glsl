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
layout(location = 5) out vec3 fragWorldPos; // VK-1526: world position for PBR view vector

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
    // VK-1526: PBR material params (identical in vertex + fragment blocks; only the fragment stage reads them).
    uint materialFlags;
    float metallic;
    float roughness;
    float ao;
    float emissionStrength;
    float albedoTintR;
    float albedoTintG;
    float albedoTintB;
    float albedoTintA;
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
    fragWorldPos = finalPos;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragLifetimeRatio;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in float fragGlowIntensity;
layout(location = 5) in vec3 fragWorldPos; // VK-1526

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
} camera;

layout(binding = 1) uniform sampler2D particleTexture;    // .vfImage albedo (legacy path)
// VK-1526: material PBR maps (bound by setMaterial; sampled only on the HAS_MATERIAL branch).
layout(binding = 2) uniform sampler2D matAlbedoTexture;
layout(binding = 3) uniform sampler2D matNormalTexture;
layout(binding = 4) uniform sampler2D matOrmTexture;
layout(binding = 5) uniform sampler2D matEmissiveTexture;

// Engine canonical BRDF primitives (distributionGGX/geometrySmith/fresnelSchlickDirect/LIGHTING_PI) +
// the shared VFX surface assembly. Both are binding-free, so they compile in this preview context.
#include "../common/lighting_functions.glsl"
#include "vfx_pbr_shading.glsl"

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
    // VK-1526: PBR material params.
    uint materialFlags;
    float metallic;
    float roughness;
    float ao;
    float emissionStrength;
    float albedoTintR;
    float albedoTintG;
    float albedoTintB;
    float albedoTintA;
} pc;

void main() {
    vec4 finalColor;

    if ((pc.materialFlags & VFX_MAT_HAS_MATERIAL) != 0u) {
        // VK-1526 PBR path: same surface assembly + BRDF core as the runtime shader (parity by construction).
        // Preview integrates a single key directional light; runtime additionally integrates clustered scene
        // lights (that difference is the documented preview<->runtime boundary).
        vec4 baseTexel     = texture(matAlbedoTexture, fragTexCoord);
        vec3 normalTexel   = texture(matNormalTexture, fragTexCoord).xyz;
        vec3 ormTexel      = texture(matOrmTexture, fragTexCoord).rgb;
        vec3 emissiveTexel = texture(matEmissiveTexture, fragTexCoord).rgb;

        VFXSurface surf = vfxBuildMeshSurface(
            pc.materialFlags,
            baseTexel, normalTexel, ormTexel, emissiveTexel,
            pc.metallic, pc.roughness, pc.ao, pc.emissionStrength,
            vec4(pc.albedoTintR, pc.albedoTintG, pc.albedoTintB, pc.albedoTintA), fragColor,
            fragNormal, fragWorldPos, fragTexCoord);

        vec3 L = normalize(vec3(0.5, 1.0, 0.3));
        vec3 V = normalize(camera.cameraPos - fragWorldPos);
        vec3 N = surf.N;
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        float Dg = distributionGGX(NdotH, surf.roughness);
        float Gg = geometrySmith(NdotV, NdotL, surf.roughness);
        vec3 Fg = fresnelSchlickDirect(HdotV, surf.F0);
        vec3 spec = (Dg * Gg * Fg) / (4.0 * NdotV * NdotL + 0.0001);
        vec3 kD = (vec3(1.0) - Fg) * (1.0 - surf.metallic);

        vec3 radiance = vec3(2.5); // preview key-light intensity
        vec3 lit = surf.albedo * surf.ao * 0.3
                 + (kD * surf.albedo / LIGHTING_PI + spec) * radiance * NdotL;
        finalColor = vec4(lit + surf.emissive, surf.alpha);
    } else {
        // Legacy single-.vfImage path — byte-identical to pre-VK-1526.
        vec4 texColor = texture(particleTexture, fragTexCoord);
        finalColor = texColor * fragColor;

        // Basic directional lighting
        vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
        vec3 normal = normalize(fragNormal);
        float diffuse = max(dot(normal, lightDir), 0.0) * 0.6 + 0.4;
        finalColor.rgb *= diffuse;
    }

    // ---- Shared tail (unchanged VFX niceties): lifetime fade / glow / alpha clip / blend ----

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
