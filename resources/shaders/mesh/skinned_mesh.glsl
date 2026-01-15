#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in ivec4 inBoneIndices;
layout(location = 4) in vec4 inBoneWeights;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragSkinDebug; // 0 = no skinning, 1 = skinned
layout(location = 4) out vec3 fragSkinOffset; // DEBUG: how much skinning moved the vertex

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

// Bone matrices SSBO (set 2)
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
    // Apply bone transforms
    mat4 skinMatrix = mat4(0.0);
    float totalWeight = 0.0;

    // Debug: count how many valid bone influences
    int validBoneCount = 0;

    for (int i = 0; i < 4; ++i) {
        int boneIdx = inBoneIndices[i];
        float weight = inBoneWeights[i];

        if (boneIdx >= 0 && boneIdx < int(bones.activeBoneCount) && weight > 0.0) {
            skinMatrix += bones.boneMatrices[boneIdx] * weight;
            totalWeight += weight;
            validBoneCount++;
        }
    }

    // Normalize or use identity if no valid bones
    if (totalWeight > 0.0001) {
        // Normalize to handle cases where some bone influences were filtered out
        skinMatrix /= totalWeight;
    } else {
        skinMatrix = mat4(1.0);
    }

    // Apply skin transform then model transform
    vec4 skinnedPos = skinMatrix * vec4(inPosition, 1.0);
    vec4 worldPos = pc.model * skinnedPos;
    fragWorldPos = worldPos.xyz;

    // Transform normal through skin matrix then to world space
    mat3 skinNormalMatrix = transpose(inverse(mat3(skinMatrix)));
    vec3 skinnedNormal = normalize(skinNormalMatrix * inNormal);

    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    fragNormal = normalize(normalMatrix * skinnedNormal);

    fragTexCoord = inTexCoord;

    // Debug output: 1.0 if skinning applied, 0.0 if identity fallback
    fragSkinDebug = (totalWeight > 0.0001) ? 1.0 : 0.0;

    // DEBUG: How much did skinning move this vertex?
    fragSkinOffset = skinnedPos.xyz - inPosition;

    gl_Position = camera.projection * camera.view * worldPos;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragSkinDebug;
layout(location = 4) in vec3 fragSkinOffset;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

// Set 0: Global resources (camera, IBL)
layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Set 1: Material textures (16 per material)
layout(set = 1, binding = 0) uniform sampler2D u_Textures[16];

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
    float emission;
} pc;

const float PI = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0;

// Normal Distribution Function (GGX/Trowbridge-Reitz)
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

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);
    vec3 R = reflect(-V, N);

    // Use material properties from push constants
    vec3 albedo = pc.albedo.rgb;
    float alpha = pc.albedo.a;
    float metallic = pc.metallic;
    float roughness = pc.roughness;
    float ao = pc.ao;

    // Calculate F0 (reflectance at normal incidence)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // IBL Ambient Lighting
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    // Diffuse IBL (irradiance)
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;

    // Specular IBL (prefiltered environment + BRDF)
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    // Combine ambient
    vec3 ambient = (kD * diffuse + specular) * ao;

    // Add emission
    vec3 emissive = albedo * pc.emission;

    vec3 color = ambient + emissive;

    // Tone mapping
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    // DEBUG visualization disabled - animation working
    // Uncomment to debug skinning:
    // float offsetMagnitude = length(fragSkinOffset);
    // if (fragSkinDebug < 0.5) color = vec3(1.0, 0.0, 0.0);      // RED = no weights
    // else if (offsetMagnitude < 0.01) color = vec3(1.0, 0.0, 1.0); // MAGENTA = no movement
    // else if (offsetMagnitude < 1.0) color = vec3(1.0, 1.0, 0.0);  // YELLOW = small
    // else color = vec3(0.0, 1.0, 0.0);                             // GREEN = working

    outColor = vec4(color, alpha);
}
