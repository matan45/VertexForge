#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
    float emission;
    // Texture indices: < 0 = no texture, >= 0 = index in u_Textures[8]
    float albedoTexIdx;
    float metallicTexIdx;
    float roughnessTexIdx;
    float aoTexIdx;
    float normalTexIdx;
    float emissionTexIdx;
} pc;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform normal to world space (using normal matrix)
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    fragNormal = normalize(normalMatrix * inNormal);

    fragTexCoord = inTexCoord;

    gl_Position = camera.projection * camera.view * worldPos;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
} camera;

// Set 0: Global resources (camera, IBL)
layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Set 1: Material textures (8 slots)
layout(set = 1, binding = 0) uniform sampler2D u_Textures[8];

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
    float emission;
    // Texture indices: < 0 = no texture, >= 0 = index in u_Textures[8]
    float albedoTexIdx;
    float metallicTexIdx;
    float roughnessTexIdx;
    float aoTexIdx;
    float normalTexIdx;
    float emissionTexIdx;
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

// Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

// Smith's method for geometry
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness for IBL
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    // Sample textures or use push constant values
    vec3 albedo = pc.albedo.rgb;
    if (pc.albedoTexIdx >= 0.0) {
        albedo = texture(u_Textures[int(pc.albedoTexIdx)], fragTexCoord).rgb;
    }

    float metallic = pc.metallic;
    if (pc.metallicTexIdx >= 0.0) {
        metallic = texture(u_Textures[int(pc.metallicTexIdx)], fragTexCoord).r;
    }

    float roughness = pc.roughness;
    if (pc.roughnessTexIdx >= 0.0) {
        roughness = texture(u_Textures[int(pc.roughnessTexIdx)], fragTexCoord).r;
    }

    float ao = pc.ao;
    if (pc.aoTexIdx >= 0.0) {
        ao = texture(u_Textures[int(pc.aoTexIdx)], fragTexCoord).r;
    }

    // Normal mapping
    if (pc.normalTexIdx >= 0.0) {
        vec3 tangentNormal = texture(u_Textures[int(pc.normalTexIdx)], fragTexCoord).rgb * 2.0 - 1.0;
        // Simple normal perturbation (proper TBN would require tangent/bitangent)
        N = normalize(N + tangentNormal * 0.5);
    }

    vec3 R = reflect(-V, N);

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
    vec3 emissive = vec3(0.0);
    if (pc.emissionTexIdx >= 0.0) {
        emissive = texture(u_Textures[int(pc.emissionTexIdx)], fragTexCoord).rgb;
    } else {
        emissive = albedo * pc.emission;
    }

    vec3 color = ambient + emissive;

    // HDR tonemapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, pc.albedo.a);
}
