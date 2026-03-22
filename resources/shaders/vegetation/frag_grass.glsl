#type FRAGMENT
#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in float inAlpha;
layout(location = 4) flat in uint inVegType;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec4 baseColor;
    vec4 tipColor;
    float fadeStartDistance;
    float fadeEndDistance;
    float sssDistortion;
    float sssPower;
    float sssScale;
    uint billboardTextureIndex;
};

// Camera (set 1)
layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    vec4 frustumPlanes[6];
};

// Light data (set 3) - matches GPULightBufferManager layout
struct DirectionalLight {
    vec3 direction;
    float intensity;
    vec3 color;
    int shadowIndex;
};

struct LightCounts {
    uint directionalCount;
    uint pointCount;
    uint spotCount;
    float shadowIntensity;
};

layout(std430, set = 3, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight directionalLights[];
};

layout(std140, set = 3, binding = 3) uniform LightCountsUBO {
    LightCounts lightCounts;
};

// Bindless textures (set 4) for billboard vegetation
layout(set = 4, binding = 0) uniform sampler2D bindlessTextures[];

// Wrap diffuse: extends light past the terminator for thin surfaces
float wrapDiffuse(float NdotL, float wrap) {
    return max(0.0, (NdotL + wrap) / (1.0 + wrap));
}

// SSS approximation: view-dependent translucency for backlit thin geometry
vec3 subsurfaceScattering(vec3 N, vec3 L, vec3 V, vec3 lightColor,
                           float lightIntensity, vec3 albedo) {
    vec3 vLTLight = L + N * sssDistortion;
    float fLTDot = pow(clamp(dot(V, -vLTLight), 0.0, 1.0), sssPower) * sssScale;
    return albedo * lightColor * lightIntensity * fLTDot;
}

void main() {
    if (inAlpha < 0.01) discard;

    // Two-sided normal flipping
    vec3 N = normalize(inNormal);
    if (!gl_FrontFacing) {
        N = -N;
    }

    vec3 V = normalize(cameraPos - inWorldPos);

    // Gradient from base to tip using config colors
    vec3 albedo = mix(baseColor.rgb, tipColor.rgb, inUV.y);

    // Billboard texture sampling (type 1, when billboardTextureIndex is valid)
    float finalAlpha = inAlpha;
    if (inVegType == 1u && billboardTextureIndex != 0xFFFFFFFFu) {
        vec4 texColor = texture(bindlessTextures[nonuniformEXT(billboardTextureIndex)], inUV);
        albedo = texColor.rgb;
        finalAlpha *= texColor.a;
        if (finalAlpha < 0.1) discard;
    }

    // Ambient term
    float ambient = 0.15;
    vec3 totalLight = albedo * ambient;

    // Evaluate directional lights
    uint dirCount = min(lightCounts.directionalCount, 4u);
    for (uint i = 0; i < dirCount; i++) {
        DirectionalLight light = directionalLights[i];
        vec3 L = -normalize(light.direction);

        // Wrap diffuse (extends lighting 30% past terminator for soft look)
        float NdotL = dot(N, L);
        float diffuse = wrapDiffuse(NdotL, 0.3);

        // Front-face direct diffuse
        vec3 directLight = albedo * light.color * light.intensity * diffuse;

        // SSS translucency (backlit glow through thin blades)
        vec3 sss = subsurfaceScattering(N, L, V, light.color, light.intensity, albedo);

        // Height-based thickness modulation: tips are thinner, more translucent
        float thicknessFactor = mix(0.3, 1.0, inUV.y);
        sss *= thicknessFactor;

        totalLight += directLight + sss;
    }

    // Fallback if no directional lights in scene
    if (dirCount == 0u) {
        vec3 fallbackDir = normalize(vec3(0.5, 1.0, 0.3));
        float NdotL = dot(N, fallbackDir);
        float diffuse = wrapDiffuse(NdotL, 0.3);
        totalLight += albedo * diffuse * 0.6;
    }

    outColor = vec4(totalLight, finalAlpha);
}
