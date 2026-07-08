#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;
layout(location = 3) out float fragViewDepth;
layout(location = 4) out vec3 fragNormal;
layout(location = 5) out vec3 fragWorldPos;
layout(location = 6) out float fragGlowIntensity;

#include "vfx_gpu_types.glsl"
#include "vfx_mesh_orientation.glsl"

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    float nearPlane;
    float farPlane;
    float _pad1;
    float _pad2;
} camera;

layout(std430, set = 0, binding = 2) readonly buffer ParticleBuffer {
    GPUParticle particles[];
};

layout(std430, set = 0, binding = 3) readonly buffer EmitterConfigBuffer {
    GPUEmitterConfig configs[];
};

layout(push_constant) uniform PushConstants {
    uint emitterIndex;
    float alphaClipThreshold;
    uint blendMode;
    float glowColorR;
    float glowColorG;
    float glowColorB;
} pc;

void main() {
    uint particleIdx = gl_InstanceIndex;

    GPUParticle p = particles[particleIdx];

    // Cull dead particles
    if (p.size <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragColor = vec4(0.0);
        fragLifetimeRatio = 1.0;
        fragViewDepth = 0.0;
        fragNormal = vec3(0.0);
        fragWorldPos = vec3(0.0);
        fragGlowIntensity = 0.0;
        return;
    }

    GPUEmitterConfig config = configs[pc.emitterIndex];

    // VK-1476: orientation basis from the emitter's mode (default = velocity-forward,
    // byte-identical to the legacy formula). camBasis columns = world-space camera
    // right/up/toward-camera (rows of the view 3x3), consumed by CameraFacing.
    mat3 camBasis = transpose(mat3(camera.view));
    mat3 rotationMatrix = vfxComputeMeshOrientation(
        config.meshOrientationMode, p.velocity, p.rotation, p.spawnSeed,
        p.lifetime, config.meshOrientationParams, camBasis);

    // Scale and transform mesh vertex
    vec3 scaledPos = inPosition * p.size;
    vec3 worldPos = p.position + rotationMatrix * scaledPos;

    gl_Position = camera.projection * camera.view * vec4(worldPos, 1.0);

    // View-space depth for soft particles
    fragViewDepth = -(camera.view * vec4(worldPos, 1.0)).z;

    fragTexCoord = inTexCoord + vec2(config.uvScrollSpeedU, config.uvScrollSpeedV) * camera.time;
    fragColor = p.color;
    fragLifetimeRatio = (p.maxLifetime > 0.0) ? (p.lifetime / p.maxLifetime) : 0.0;
    fragNormal = rotationMatrix * inNormal;
    fragWorldPos = worldPos;
    fragGlowIntensity = p.glowIntensity;
}

#type FRAGMENT
#version 460 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragLifetimeRatio;
layout(location = 3) in float fragViewDepth;
layout(location = 4) in vec3 fragNormal;
layout(location = 5) in vec3 fragWorldPos;
layout(location = 6) in float fragGlowIntensity;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    float nearPlane;
    float farPlane;
    float _pad1;
    float _pad2;
} camera;

// VK-1481: shared VFX bindless texture table (bound once per pass). Per-emitter slot in pc.textureIndex.
layout(set = 4, binding = 0) uniform sampler2D bindlessTextures[];

#include "vfx_gpu_types.glsl"

layout(std430, set = 0, binding = 3) readonly buffer EmitterConfigBuffer {
    GPUEmitterConfig configs[];
};

layout(binding = 4) uniform sampler2D sceneDepthTexture;

// Lighting types (struct definitions + cluster helpers)
#include "../common/lighting_functions.glsl"
#include "../common/cluster_culling.glsl"

// Lighting descriptor sets (shared from main renderer)
layout(std430, set = 1, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight directionalLights[];
};

layout(std430, set = 1, binding = 1) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

layout(std430, set = 1, binding = 2) readonly buffer SpotLightBuffer {
    SpotLight spotLights[];
};

layout(std140, set = 1, binding = 3) uniform LightCountsUBO {
    LightCounts lightCounts;
};

layout(std140, set = 2, binding = 0) uniform ClusterParamsUBO {
    ClusterGridParams clusterParams;
};

layout(std430, set = 3, binding = 0) readonly buffer ClusterLightGridBuffer {
    ClusterLightData clusterLightGrid[];
};

layout(std430, set = 3, binding = 1) readonly buffer ClusterLightIndexListBuffer {
    uint lightIndexList[];
};

// VFX lighting evaluation (must come after buffer declarations above)
#include "vfx_lighting.glsl"

layout(push_constant) uniform PushConstants {
    uint emitterIndex;
    float alphaClipThreshold;
    uint blendMode;
    float glowColorR;
    float glowColorG;
    float glowColorB;
    uint textureIndex;
} pc;

void main() {
    vec4 texColor = texture(bindlessTextures[nonuniformEXT(pc.textureIndex)], fragTexCoord);
    vec4 finalColor = texColor * fragColor;

    GPUEmitterConfig config = configs[pc.emitterIndex];
    vec3 normal = normalize(fragNormal);

    // Lighting: use scene lights when available, fallback to hard-coded for unlit
    if (config.lightingInfluence > 0.0 && pc.blendMode != 1u) {
        finalColor.rgb = evaluateVFXLighting(
            finalColor.rgb, normal, fragWorldPos, fragViewDepth,
            config.ambientAmount, config.lightingInfluence);
    } else {
        // Fallback: basic hard-coded directional light for unlit mesh particles
        vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
        float diffuse = max(dot(normal, lightDir), 0.0) * 0.6 + 0.4;
        finalColor.rgb *= diffuse;
    }

    // Soft particles: fade near scene geometry
    if (config.softParticleDistance > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneDepthTexture, 0));
        float rawDepth = texture(sceneDepthTexture, screenUV).r;

        float sceneLinearDepth = camera.nearPlane * camera.farPlane /
            (camera.farPlane - rawDepth * (camera.farPlane - camera.nearPlane));

        float depthDiff = sceneLinearDepth - fragViewDepth;
        float softFactor = clamp(depthDiff / config.softParticleDistance, 0.0, 1.0);
        finalColor.a *= softFactor;
    }

    // Glow: additive emissive color (applied after lighting)
    vec3 glowColor = vec3(pc.glowColorR, pc.glowColorG, pc.glowColorB);
    finalColor.rgb += glowColor * fragGlowIntensity;
    finalColor.rgb *= config.emissiveIntensity;

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
