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
    float u_Time;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
    float emission;
    uint textureIndicesPacked[4];
    float blendMode;  // 0=Opaque, 1=Masked, 2=Translucent
    float alphaCutoff;
    float iblDiffuse;
    float iblSpecular;
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
    float u_Time;
} camera;

// Set 0: Global resources (camera, IBL)
layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Set 1: Material textures (16 per material)
// See TextureSlot enum for slot assignments
layout(set = 1, binding = 0) uniform sampler2D u_Textures[16];

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
    float emission;
    uint textureIndicesPacked[4];
    float blendMode;  // 0=Opaque, 1=Masked, 2=Translucent
    float alphaCutoff;
    float iblDiffuse;
    float iblSpecular;
} pc;

const float PI = 3.14159265359;
// Alpha threshold for masked mode - now read from push constants (pc.alphaCutoff)
const float MAX_REFLECTION_LOD = 4.0;
const uint TEXTURE_INDEX_NONE = 255u;

// Texture slot indices (matching TextureSlot enum in MaterialTypes.hpp)
const uint SLOT_ALBEDO = 0u;
const uint SLOT_NORMAL = 1u;
const uint SLOT_ORM = 2u;
const uint SLOT_METALLIC = 3u;
const uint SLOT_ROUGHNESS = 4u;
const uint SLOT_AO = 5u;
const uint SLOT_EMISSION = 6u;
const uint SLOT_HEIGHT = 7u;

// Unpack texture index from packed uint32 array
// slot: 0-15, returns 255 if no texture
uint unpackTextureIndex(uint slot) {
    uint packIdx = slot / 4u;
    uint byteOffset = slot % 4u;
    return (pc.textureIndicesPacked[packIdx] >> (byteOffset * 8u)) & 0xFFu;
}

bool hasTexture(uint slot) {
    return unpackTextureIndex(slot) != TEXTURE_INDEX_NONE;
}

// Unpack ORM texture: R=AO, G=Roughness, B=Metallic
vec3 unpackORM(vec4 ormSample) {
    return vec3(ormSample.r, ormSample.g, ormSample.b);
}

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

    vec3 albedo = pc.albedo.rgb;
    float alpha = pc.albedo.a;
    if (hasTexture(SLOT_ALBEDO)) {
        vec4 albedoSample = texture(u_Textures[SLOT_ALBEDO], fragTexCoord);
        // Convert from sRGB to linear space for PBR calculations
        albedo = pow(albedoSample.rgb, vec3(2.2));
        alpha = albedoSample.a;
    }

    // Masked mode: discard fragments below alpha threshold
    if (pc.blendMode > 0.5 && pc.blendMode < 1.5) {  // blendMode == 1 (Masked)
        if (alpha < pc.alphaCutoff) {
            discard;
        }
    }

    // Sample PBR values - prioritize ORM packed texture, fallback to individual textures
    float metallic = pc.metallic;
    float roughness = pc.roughness;
    float ao = pc.ao;

    if (hasTexture(SLOT_ORM)) {
        // Use packed ORM texture: R=AO, G=Roughness, B=Metallic
        vec3 ormValues = unpackORM(texture(u_Textures[SLOT_ORM], fragTexCoord));
        ao = ormValues.x;
        roughness = ormValues.y;
        metallic = ormValues.z;
    } else {
        // Fallback to individual textures
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

    // Normal mapping using derivative-based TBN construction
    if (hasTexture(SLOT_NORMAL)) {
        vec3 tangentNormal = texture(u_Textures[SLOT_NORMAL], fragTexCoord).rgb * 2.0 - 1.0;

        // Construct TBN matrix from screen-space derivatives
        vec3 pos_dx = dFdx(fragWorldPos);
        vec3 pos_dy = dFdy(fragWorldPos);
        vec2 uv_dx = dFdx(fragTexCoord);
        vec2 uv_dy = dFdy(fragTexCoord);

        // Calculate tangent and bitangent
        vec3 T = normalize(pos_dx * uv_dy.y - pos_dy * uv_dx.y);
        vec3 B = normalize(pos_dy * uv_dx.x - pos_dx * uv_dy.x);

        // Ensure orthogonal TBN
        T = normalize(T - N * dot(N, T));
        B = cross(N, T);

        // Transform normal from tangent space to world space
        mat3 TBN = mat3(T, B, N);
        N = normalize(TBN * tangentNormal);
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
    vec3 diffuse = irradiance * albedo * pc.iblDiffuse;

    // Specular IBL (prefiltered environment + BRDF)
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y) * pc.iblSpecular;

    // Combine ambient
    vec3 ambient = (kD * diffuse + specular) * ao;

    // Add emission
    vec3 emissive = vec3(0.0);
    if (hasTexture(SLOT_EMISSION)) {
        emissive = pow(texture(u_Textures[SLOT_EMISSION], fragTexCoord).rgb, vec3(2.2)) * pc.emission;
    } else {
        emissive = albedo * pc.emission;
    }

    vec3 color = ambient + emissive;

    color = color / (color + vec3(1.0));

    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, alpha);
}
