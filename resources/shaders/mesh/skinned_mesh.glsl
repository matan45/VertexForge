#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;  // Required by vertex layout, reserved for future texture support
layout(location = 3) in ivec4 inBoneIndices;
layout(location = 4) in vec4 inBoneWeights;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

const int MAX_BONES = 128;
layout(std430, set = 2, binding = 0) readonly buffer BoneMatrices {
    mat4 boneMatrices[MAX_BONES];
    uint activeBoneCount;
    uint padding[3];
} bones;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
    float emission;
    // VK-1433: packed per-slot material texture indices (4 bytes per uint, 16 slots).
    // All-0xFF (TEXTURE_INDEX_NONE) means "no material textures" — the default state, in
    // which this shader behaves exactly as the scalar-PBR-only original. Only the prefab
    // rig preview populates these to render real material textures; the vertex stage
    // ignores them (declared here only to keep the push-constant block byte-identical
    // across both stages).
    uint textureIndicesPacked[4];
} pc;

void main() {
    mat4 skinMatrix = mat4(0.0);
    float totalWeight = 0.0;

    for (int i = 0; i < 4; ++i) {
        int boneIdx = inBoneIndices[i];
        float weight = inBoneWeights[i];

        if (boneIdx >= 0 && boneIdx < int(bones.activeBoneCount) && weight > 0.0) {
            skinMatrix += bones.boneMatrices[boneIdx] * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0001) {
        skinMatrix /= totalWeight;
    } else {
        skinMatrix = mat4(1.0);
    }

    vec4 skinnedPos = skinMatrix * vec4(inPosition, 1.0);
    vec4 worldPos = pc.model * skinnedPos;
    fragWorldPos = worldPos.xyz;

    vec3 skinnedNormal = normalize(mat3(skinMatrix) * inNormal);
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    fragNormal = normalize(normalMatrix * skinnedNormal);
    fragTexCoord = inTexCoord;

    gl_Position = camera.projection * camera.view * worldPos;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;  // Reserved for future texture support

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

// VK-1433: material texture set. The C++ SkinnedMeshPipeline already creates this 16-sampler
// set 1 (previously bound 16x to the default BRDF-LUT and never sampled). The prefab rig
// preview rebinds these to a material's real PBR textures and packs the per-slot indices
// below; every other caller leaves the indices at TEXTURE_INDEX_NONE so this array is never
// sampled and the scalar-PBR output is byte-identical to the original shader.
layout(set = 1, binding = 0) uniform sampler2D u_Textures[16];

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
    float emission;
    uint textureIndicesPacked[4]; // see VERTEX stage comment
} pc;

const float MAX_REFLECTION_LOD = 4.0;
const uint TEXTURE_INDEX_NONE = 255u;

const uint SLOT_ALBEDO = 0u;
const uint SLOT_NORMAL = 1u;
const uint SLOT_ORM = 2u;
const uint SLOT_METALLIC = 3u;
const uint SLOT_ROUGHNESS = 4u;
const uint SLOT_AO = 5u;
const uint SLOT_EMISSION = 6u;

uint unpackTextureIndex(uint slot) {
    uint packIdx = slot / 4u;
    uint byteOffset = slot % 4u;
    return (pc.textureIndicesPacked[packIdx] >> (byteOffset * 8u)) & 0xFFu;
}

bool hasTexture(uint slot) {
    return unpackTextureIndex(slot) != TEXTURE_INDEX_NONE;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

#include "../common/ibl_functions.glsl"

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    vec3 albedo = pc.albedo.rgb;
    float alpha = pc.albedo.a;
    float metallic = pc.metallic;
    float roughness = pc.roughness;
    float ao = pc.ao;

    // Material textures (only when bound by the prefab rig preview; otherwise skipped).
    if (hasTexture(SLOT_ALBEDO)) {
        vec4 albedoSample = texture(u_Textures[SLOT_ALBEDO], fragTexCoord);
        albedo = albedoSample.rgb;
        alpha = albedoSample.a;
    }

    if (hasTexture(SLOT_ORM)) {
        vec4 ormSample = texture(u_Textures[SLOT_ORM], fragTexCoord);
        ao = ormSample.r;
        roughness = ormSample.g;
        metallic = ormSample.b;
    } else {
        if (hasTexture(SLOT_METALLIC)) {
            metallic = texture(u_Textures[SLOT_METALLIC], fragTexCoord).r;
        }
        if (hasTexture(SLOT_ROUGHNESS)) {
            roughness = texture(u_Textures[SLOT_ROUGHNESS], fragTexCoord).r;
        }
        if (hasTexture(SLOT_AO)) {
            ao = texture(u_Textures[SLOT_AO], fragTexCoord).r;
        }
    }

    if (hasTexture(SLOT_NORMAL)) {
        vec3 tangentNormal = texture(u_Textures[SLOT_NORMAL], fragTexCoord).rgb * 2.0 - 1.0;

        vec3 pos_dx = dFdx(fragWorldPos);
        vec3 pos_dy = dFdy(fragWorldPos);
        vec2 uv_dx = dFdx(fragTexCoord);
        vec2 uv_dy = dFdy(fragTexCoord);

        vec3 T = normalize(pos_dx * uv_dy.y - pos_dy * uv_dx.y);
        vec3 B = normalize(pos_dy * uv_dx.x - pos_dx * uv_dy.x);
        T = normalize(T - N * dot(N, T));
        B = cross(N, T);

        mat3 TBN = mat3(T, B, N);
        N = normalize(TBN * tangentNormal);
    }

    vec3 R = reflect(-V, N);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NdotV = max(dot(N, V), 0.0);
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;

    vec3 specularScale;
    vec3 kD;
    multiScatterCompensation(F0, brdf, metallic, specularScale, kD);

    vec3 diffuse = irradiance * albedo;
    vec3 specular = prefilteredColor * specularScale;

    float so = specularOcclusion(NdotV, ao, roughness);
    vec3 ambient = kD * diffuse * ao + specular * so;
    vec3 emissive = hasTexture(SLOT_EMISSION)
        ? texture(u_Textures[SLOT_EMISSION], fragTexCoord).rgb * pc.emission
        : albedo * pc.emission;
    vec3 color = ambient + emissive;

    // Tonemapping and gamma handled by post-process pipeline

    outColor = vec4(color, alpha);
}
