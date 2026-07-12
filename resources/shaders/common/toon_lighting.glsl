#ifndef TOON_LIGHTING_GLSL
#define TOON_LIGHTING_GLSL

// VK-1493 — GPU-driven toon lighting glue. Included ONLY by mesh_shader_gpudriven.glsl,
// AFTER lighting_functions.glsl (needs PointLight/SpotLight/DirectionalLight + the [0,1]
// attenuation helpers smoothDistanceAttenuation / spotAngleAttenuation) and after
// toon_shading.glsl (the pure primitives + ToonProfileGPU). Declares the toon profile
// table SSBO at set 1 / binding 6 and unpacks the shading-model / profile-index bits from
// PerDrawData.flags. lighting_functions.glsl (shared with terrain) is never modified.

layout(std430, set = 1, binding = 6) readonly buffer ToonProfileTableBuffer {
    ToonProfileGPU toonProfiles[];
};

// Must match ObjectFlags::{ShadingModel,ProfileIndex}* + packShadingFlags in GPUDrivenTypes.hpp.
const uint SHADING_MODEL_SHIFT = 23u;
const uint SHADING_MODEL_MASK  = 0x3u;
const uint PROFILE_INDEX_SHIFT = 25u;
const uint PROFILE_INDEX_MASK  = 0x7Fu;
const uint SHADING_MODEL_TOON  = 2u;

uint getShadingModel(uint flags)     { return (flags >> SHADING_MODEL_SHIFT) & SHADING_MODEL_MASK; }
uint getToonProfileIndex(uint flags) { return (flags >> PROFILE_INDEX_SHIFT) & PROFILE_INDEX_MASK; }

// Per-light wrappers. Each returns the banded diffuse contribution and writes its
// specular blob through specOut. Lit-band color is albedo * lightColor — light intensity
// is intentionally decoupled so band boundaries do not shift with brightness and the
// luminance-max combine stays well-conditioned. L conventions match lighting_functions.glsl.
vec3 toonPointLight(ToonProfileGPU p, vec3 worldPos, vec3 N, vec3 V, vec3 albedo,
                    PointLight light, float shadow, out vec3 specOut) {
    vec3 Lv = light.position - worldPos;
    float d = length(Lv);
    vec3 L = Lv / max(d, 1e-4);
    float atten01 = smoothDistanceAttenuation(d, light.radius); // 0 beyond radius -> shade band
    specOut = toonSpecBlob(p, N, V, L, atten01, shadow);
    return toonApplyBands(p, dot(N, L), atten01, shadow, albedo * light.color);
}

vec3 toonSpotLight(ToonProfileGPU p, vec3 worldPos, vec3 N, vec3 V, vec3 albedo,
                   SpotLight light, float shadow, out vec3 specOut) {
    vec3 Lv = light.position - worldPos;
    float d = length(Lv);
    vec3 L = Lv / max(d, 1e-4);
    float atten01 = smoothDistanceAttenuation(d, light.range)
                  * spotAngleAttenuation(L, light.direction, light.cosInnerAngle, light.cosOuterAngle);
    specOut = toonSpecBlob(p, N, V, L, atten01, shadow);
    return toonApplyBands(p, dot(N, L), atten01, shadow, albedo * light.color);
}

vec3 toonDirectionalLight(ToonProfileGPU p, vec3 N, vec3 V, vec3 albedo,
                          DirectionalLight light, float shadow, out vec3 specOut) {
    vec3 L = -normalize(light.direction);
    specOut = toonSpecBlob(p, N, V, L, 1.0, shadow);
    return toonApplyBands(p, dot(N, L), 1.0, shadow, albedo * light.color);
}

#endif // TOON_LIGHTING_GLSL
