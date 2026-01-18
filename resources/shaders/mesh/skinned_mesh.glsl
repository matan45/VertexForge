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

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
    float emission;
} pc;

const float MAX_REFLECTION_LOD = 4.0;

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);
    vec3 R = reflect(-V, N);

    vec3 albedo = pc.albedo.rgb;
    float alpha = pc.albedo.a;
    float metallic = pc.metallic;
    float roughness = pc.roughness;
    float ao = pc.ao;

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;

    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuse + specular) * ao;
    vec3 emissive = albedo * pc.emission;
    vec3 color = ambient + emissive;

    // Tone mapping and gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, alpha);
}
