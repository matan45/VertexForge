#type FRAGMENT
#version 460 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in float inRoughness;
layout(location = 0) out vec4 outNormal;

#ifdef ALBEDO_PREPASS
// DLSS-D Ray Reconstruction demodulation guides (VK-1397). The scene prepass
// emits diffuse + specular albedo so Ray Reconstruction can separate lighting
// from base material. Terrain reuses this shader without ALBEDO_PREPASS, so it
// writes only the normal attachment (albedo targets keep their clear value).
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in float inNoV;
layout(location = 4) in flat uint inAlbedoIdx;
layout(location = 5) in flat float inMetallic;
layout(location = 6) in flat vec3 inBaseColor;

layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

layout(location = 1) out vec4 outDiffuseAlbedo;
layout(location = 2) out vec4 outSpecularAlbedo;

bool isValidTexture(uint index) {
    return index != 0xFFFFFFFFu && index != 0xFFu && index < 4096u;
}

// Karis' analytic environment BRDF approximation (UE4 mobile). Produces the
// pre-integrated specular reflectance Ray Reconstruction expects as the
// specular-albedo guide (equivalent to the SDK's EnvBRDFApprox2 helper).
vec3 envBRDFApprox(vec3 F0, float roughness, float NoV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}
#endif

void main() {
    outNormal = vec4(normalize(inWorldNormal), inRoughness);

#ifdef ALBEDO_PREPASS
    vec3 albedo = inBaseColor;
    if (isValidTexture(inAlbedoIdx)) {
        albedo = texture(bindlessTextures[nonuniformEXT(inAlbedoIdx)], inTexCoord).rgb;
    }
    vec3 F0 = mix(vec3(0.04), albedo, inMetallic);
    outDiffuseAlbedo = vec4(albedo * (1.0 - inMetallic), 1.0);
    outSpecularAlbedo = vec4(envBRDFApprox(F0, inRoughness, inNoV), 1.0);
#endif
}
