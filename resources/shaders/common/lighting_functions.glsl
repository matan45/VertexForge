#ifndef LIGHTING_FUNCTIONS_GLSL
#define LIGHTING_FUNCTIONS_GLSL

// PBR Lighting Functions
// Shared between mesh_shader_gpudriven.glsl and mesh_terrain.glsl

const float LIGHTING_PI = 3.14159265359;
const float LIGHT_INTENSITY_SCALE = 100.0;
const float MAX_REFLECTION_LOD = 4.0;

//-----------------------------------------------------------------------------
// Light Structures (must match GPULightTypes.hpp)
//-----------------------------------------------------------------------------

// Matches GPUDirectionalLight (48 bytes)
struct DirectionalLight {
    vec3 direction;
    float intensity;
    vec3 color;
    int shadowIndex;
    int shadowMode;   // 0 = cascade, 1 = clipmap
    uint _pad[3];
};

// Matches GPUPointLight (48 bytes)
struct PointLight {
    vec3 position;
    float radius;
    vec3 color;
    float intensity;
    int shadowIndex;
    uint padding0;
    uint padding1;
    uint padding2;
};

// Matches GPUSpotLight (64 bytes)
struct SpotLight {
    vec3 position;
    float range;
    vec3 direction;
    float intensity;
    vec3 color;
    float cosInnerAngle;
    float cosOuterAngle;
    int shadowIndex;
    uint padding0;
    uint padding1;
};

struct LightCounts {
    uint directionalCount;
    uint pointCount;
    uint spotCount;
    float shadowIntensity;
};

//-----------------------------------------------------------------------------
// Attenuation Functions
//-----------------------------------------------------------------------------

float smoothDistanceAttenuation(float distance, float range) {
    float distRatio = distance / range;
    float attenuation = clamp(1.0 - distRatio * distRatio, 0.0, 1.0);
    return attenuation * attenuation;
}

float physicalAttenuation(float distance, float range) {
    float windowFn = smoothDistanceAttenuation(distance, range);
    float distAtt = LIGHT_INTENSITY_SCALE / max(distance * distance, 0.0001);
    return distAtt * windowFn;
}

float spotAngleAttenuation(vec3 lightDir, vec3 spotDir, float cosInner, float cosOuter) {
    float cosAngle = dot(-lightDir, spotDir);

    if (cosInner <= cosOuter) {
        return cosAngle >= cosOuter ? 1.0 : 0.0;
    }

    return clamp((cosAngle - cosOuter) / (cosInner - cosOuter), 0.0, 1.0);
}

//-----------------------------------------------------------------------------
// PBR BRDF Functions
//-----------------------------------------------------------------------------

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (LIGHTING_PI * denom * denom);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlickDirect(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

#include "ibl_functions.glsl"

//-----------------------------------------------------------------------------
// Light Evaluation Functions
//-----------------------------------------------------------------------------

vec3 evaluatePointLight(vec3 worldPos, vec3 N, vec3 V, vec3 albedo,
                        float metallic, float roughness, vec3 F0,
                        PointLight light) {
    vec3 L = light.position - worldPos;
    float distance = length(L);

    if (distance > light.radius) return vec3(0.0);

    L = normalize(L);

    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float attenuation = physicalAttenuation(distance, light.radius);
    vec3 radiance = light.color * light.intensity * attenuation;

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlickDirect(HdotV, F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specularBRDF = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / LIGHTING_PI + specularBRDF) * radiance * NdotL;
}

vec3 evaluateSpotLight(vec3 worldPos, vec3 N, vec3 V, vec3 albedo,
                       float metallic, float roughness, vec3 F0,
                       SpotLight light) {
    vec3 L = light.position - worldPos;
    float distance = length(L);

    if (distance > light.range) return vec3(0.0);

    L = normalize(L);

    float spotAtt = spotAngleAttenuation(L, light.direction, light.cosInnerAngle, light.cosOuterAngle);
    if (spotAtt <= 0.0) return vec3(0.0);

    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float distAtt = physicalAttenuation(distance, light.range);
    vec3 radiance = light.color * light.intensity * distAtt * spotAtt;

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlickDirect(HdotV, F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specularBRDF = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / LIGHTING_PI + specularBRDF) * radiance * NdotL;
}

vec3 evaluateDirectionalLight(vec3 N, vec3 V, vec3 albedo,
                              float metallic, float roughness, vec3 F0,
                              DirectionalLight light) {
    vec3 L = -normalize(light.direction);

    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    vec3 radiance = light.color * light.intensity;

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlickDirect(HdotV, F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specularBRDF = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / LIGHTING_PI + specularBRDF) * radiance * NdotL;
}

#endif // LIGHTING_FUNCTIONS_GLSL
