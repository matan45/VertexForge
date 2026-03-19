#type FRAGMENT
#version 460 core

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Material textures (16 per material)
// See TextureSlot enum for slot assignments:
// 0: Albedo, 1: Normal, 2: ORM (packed), 3: Metallic, 4: Roughness, 5: AO, 6: Emission, 7: Height, etc.
layout(set = 1, binding = 0) uniform sampler2D u_Textures[16];

const float PI = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0;

// Texture slot indices (matching TextureSlot enum in MaterialTypes.hpp)
const int TEX_SLOT_ALBEDO = 0;
const int TEX_SLOT_NORMAL = 1;
const int TEX_SLOT_ORM = 2;
const int TEX_SLOT_METALLIC = 3;
const int TEX_SLOT_ROUGHNESS = 4;
const int TEX_SLOT_AO = 5;
const int TEX_SLOT_EMISSION = 6;
const int TEX_SLOT_HEIGHT = 7;

// Unpack ORM texture: R=AO, G=Roughness, B=Metallic (A unused)
vec3 unpackORM(vec4 ormSample) {
    return ormSample.rgb; // AO, Roughness, Metallic
}

// PBR Functions
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Multi-scattering energy compensation (Fdez-Aguera 2019)
void multiScatterCompensation(vec3 F0, vec2 brdfLookup, float metallic,
                              out vec3 specularScale, out vec3 kD) {
    vec3 FssEss = F0 * brdfLookup.x + brdfLookup.y;
    float Ess = brdfLookup.x + brdfLookup.y;
    float Ems = 1.0 - Ess;
    vec3 Favg = F0 + (1.0 - F0) / 21.0;
    vec3 FmsEms = Ems * FssEss * Favg / (1.0 - Favg * Ems);
    specularScale = FssEss + FmsEms;
    kD = (1.0 - FssEss - FmsEms) * (1.0 - metallic);
}

// Specular occlusion from AO (Lagarde/de Rousiers, Frostbite 2014)
float specularOcclusion(float NdotV, float ao, float roughness) {
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

// Parallax Occlusion Mapping - only compiled when USE_PARALLAX is defined
#ifdef USE_PARALLAX
// Returns offset UV coordinates based on height map
vec2 parallaxOcclusionMapping(vec2 texCoord, vec3 viewDirTangent, float heightScale) {
    const float minLayers = 8.0;
    const float maxLayers = 32.0;
    float numLayers = mix(maxLayers, minLayers, abs(dot(vec3(0.0, 0.0, 1.0), viewDirTangent)));

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;
    vec2 P = viewDirTangent.xy / viewDirTangent.z * heightScale;
    vec2 deltaTexCoord = P / numLayers;

    vec2 currentTexCoord = texCoord;
    float currentDepthMapValue = texture(u_Textures[TEX_SLOT_HEIGHT], currentTexCoord).r;

    while (currentLayerDepth < currentDepthMapValue) {
        currentTexCoord -= deltaTexCoord;
        currentDepthMapValue = texture(u_Textures[TEX_SLOT_HEIGHT], currentTexCoord).r;
        currentLayerDepth += layerDepth;
    }

    // Interpolation for smoother result
    vec2 prevTexCoord = currentTexCoord + deltaTexCoord;
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = texture(u_Textures[TEX_SLOT_HEIGHT], prevTexCoord).r - currentLayerDepth + layerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth);

    return mix(currentTexCoord, prevTexCoord, weight);
}
#endif

void main() {
