#type VERTEX
#version 460 core

// GPU-Driven Mesh Vertex Shader
// Fetches per-draw data from storage buffer instead of push constants

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out flat uint fragDrawIndex;

// Set 0: Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

// Per-draw data (written by GPU cull+LOD compute shader)
struct PerDrawData {
    mat4 modelMatrix;           // 64 bytes

    vec4 albedo;                // 16 bytes
    vec4 materialParams;        // 16 bytes - metallic, roughness, ao, emission

    uvec4 textureIndices0;      // 16 bytes - albedo, normal, orm, metallic
    uvec4 textureIndices1;      // 16 bytes - roughness, ao, emission, height

    uint objectIndex;           // 4 bytes
    uint flags;                 // 4 bytes
    float iblDiffuse;           // 4 bytes
    float iblSpecular;          // 4 bytes
};

// Set 1: Per-draw data buffer
layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

void main() {
    // Get draw index from gl_BaseInstance (set by firstInstance in draw command)
    // In indirect draws, firstInstance is used to identify the draw
    uint drawIndex = gl_BaseInstance;

    PerDrawData drawData = perDrawData[drawIndex];

    vec4 worldPos = drawData.modelMatrix * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform normal to world space (using normal matrix)
    mat3 normalMatrix = transpose(inverse(mat3(drawData.modelMatrix)));
    fragNormal = normalize(normalMatrix * inNormal);

    fragTexCoord = inTexCoord;
    fragDrawIndex = drawIndex;

    gl_Position = camera.projection * camera.view * worldPos;
}

#type FRAGMENT
#version 460 core

// GPU-Driven Mesh Fragment Shader
// Uses bindless textures and per-draw data from storage buffer

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in flat uint fragDrawIndex;

layout(location = 0) out vec4 outColor;

// Set 0: Camera and IBL resources
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Per-draw data structure (must match vertex shader)
struct PerDrawData {
    mat4 modelMatrix;
    vec4 albedo;
    vec4 materialParams;
    uvec4 textureIndices0;
    uvec4 textureIndices1;
    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;
};

// Set 1: Per-draw data buffer
layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

// Set 2: Bindless texture array
layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

// Constants
const float PI = 3.14159265359;
const float ALPHA_CUTOFF = 0.5;
const float MAX_REFLECTION_LOD = 4.0;
const uint INVALID_TEXTURE_INDEX = 0xFFFFFFFF;

// Texture slot indices (from textureIndices0/1)
// textureIndices0: x=albedo, y=normal, z=orm, w=metallic
// textureIndices1: x=roughness, y=ao, z=emission, w=height

// Object flags (must match ObjectFlags namespace in GPUDrivenTypes.hpp)
const uint FLAG_TRANSPARENT = 1u << 3;
const uint FLAG_ALPHA_MASK  = 1u << 4;

// Helper: Check if texture index is valid
bool isValidTexture(uint index) {
    return index != INVALID_TEXTURE_INDEX && index != 0xFFu && index < 4096u;
}

// Unpack ORM texture: R=AO, G=Roughness, B=Metallic
vec3 unpackORM(vec4 ormSample) {
    return vec3(ormSample.r, ormSample.g, ormSample.b);
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

void main() {
    PerDrawData drawData = perDrawData[fragDrawIndex];

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    // Get texture indices
    uint albedoIdx = drawData.textureIndices0.x;
    uint normalIdx = drawData.textureIndices0.y;
    uint ormIdx = drawData.textureIndices0.z;
    uint metallicIdx = drawData.textureIndices0.w;
    uint roughnessIdx = drawData.textureIndices1.x;
    uint aoIdx = drawData.textureIndices1.y;
    uint emissionIdx = drawData.textureIndices1.z;

    // Sample albedo
    vec3 albedo = drawData.albedo.rgb;
    float alpha = drawData.albedo.a;

    if (isValidTexture(albedoIdx)) {
        vec4 albedoSample = texture(bindlessTextures[nonuniformEXT(albedoIdx)], fragTexCoord);
        // Convert from sRGB to linear space for PBR calculations
        albedo = pow(albedoSample.rgb, vec3(2.2));
        alpha = albedoSample.a;
    }

    // Alpha masking
    if ((drawData.flags & FLAG_ALPHA_MASK) != 0u) {
        if (alpha < ALPHA_CUTOFF) {
            discard;
        }
    }

    // Sample PBR values - prioritize ORM packed texture, fallback to individual
    float metallic = drawData.materialParams.x;
    float roughness = drawData.materialParams.y;
    float ao = drawData.materialParams.z;
    float emission = drawData.materialParams.w;

    if (isValidTexture(ormIdx)) {
        // Use packed ORM texture: R=AO, G=Roughness, B=Metallic
        vec3 ormValues = unpackORM(texture(bindlessTextures[nonuniformEXT(ormIdx)], fragTexCoord));
        ao = ormValues.x;
        roughness = ormValues.y;
        metallic = ormValues.z;
    } else {
        // Fallback to individual textures
        if (isValidTexture(metallicIdx)) {
            metallic = texture(bindlessTextures[nonuniformEXT(metallicIdx)], fragTexCoord).r;
        }
        if (isValidTexture(roughnessIdx)) {
            roughness = texture(bindlessTextures[nonuniformEXT(roughnessIdx)], fragTexCoord).r;
        }
        if (isValidTexture(aoIdx)) {
            ao = texture(bindlessTextures[nonuniformEXT(aoIdx)], fragTexCoord).r;
        }
    }

    // Normal mapping using derivative-based TBN construction
    if (isValidTexture(normalIdx)) {
        vec3 tangentNormal = texture(bindlessTextures[nonuniformEXT(normalIdx)], fragTexCoord).rgb * 2.0 - 1.0;

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
    vec3 diffuse = irradiance * albedo * drawData.iblDiffuse;

    // Specular IBL (prefiltered environment + BRDF)
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y) * drawData.iblSpecular;

    // Combine ambient
    vec3 ambient = (kD * diffuse + specular) * ao;

    // Add emission
    vec3 emissive = vec3(0.0);
    if (isValidTexture(emissionIdx)) {
        emissive = pow(texture(bindlessTextures[nonuniformEXT(emissionIdx)], fragTexCoord).rgb, vec3(2.2)) * emission;
    } else {
        emissive = albedo * emission;
    }

    vec3 color = ambient + emissive;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, alpha);
}
