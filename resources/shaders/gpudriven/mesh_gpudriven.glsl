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
    mat4 normalMatrix;          // 64 bytes - pre-computed transpose(inverse(mat3(model)))

    vec4 albedo;                // 16 bytes
    vec4 materialParams;        // 16 bytes - metallic, roughness, ao, emission

    uvec4 textureIndices0;      // 16 bytes - albedo, normal, orm, metallic
    uvec4 textureIndices1;      // 16 bytes - roughness, ao, emission, height

    uint objectIndex;           // 4 bytes
    uint flags;                 // 4 bytes
    float iblDiffuse;           // 4 bytes
    float iblSpecular;          // 4 bytes

    uint lodLevel;              // 4 bytes - selected LOD (for debug)
    uint padding0;              // 4 bytes
    uint padding1;              // 4 bytes
    uint padding2;              // 4 bytes
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

    // Transform normal to world space using pre-computed normal matrix
    // (computed once per object in compute shader, not per vertex)
    fragNormal = normalize(mat3(drawData.normalMatrix) * inNormal);

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
    mat4 normalMatrix;
    vec4 albedo;
    vec4 materialParams;
    uvec4 textureIndices0;
    uvec4 textureIndices1;
    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;
    uint lodLevel;
    uint padding0;
    uint padding1;
    uint padding2;
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

// Parallax mapping settings
const float HEIGHT_SCALE = 0.05;        // Height scale for parallax effect
const int MIN_PARALLAX_LAYERS = 4;      // Minimum layers for parallax occlusion
const int MAX_PARALLAX_LAYERS = 12;     // Maximum layers for parallax occlusion (reduced for performance)

// Debug mode: set to 1 to visualize LOD levels with colors
// LOD0 = Green, LOD1 = Yellow, LOD2 = Orange, LOD3 = Red
const int DEBUG_VISUALIZE_LOD = 0;

// Texture slot indices (from textureIndices0/1)
// textureIndices0: x=albedo, y=normal, z=orm, w=metallic
// textureIndices1: x=roughness, y=ao, z=emission, w=height

// Object flags (must match ObjectFlags namespace in GPUDrivenTypes.hpp)
const uint FLAG_ALPHA_MASK  = 1u << 4;

// Helper: Check if texture index is valid
bool isValidTexture(uint index) {
    return index != INVALID_TEXTURE_INDEX && index != 0xFFu && index < 4096u;
}

// Unpack ORM texture: R=AO, G=Roughness, B=Metallic
vec3 unpackORM(vec4 ormSample) {
    return vec3(ormSample.r, ormSample.g, ormSample.b);
}

// Parallax Occlusion Mapping
// Returns offset texture coordinates based on height map
// Uses textureLod with LOD 0 for height samples inside loop to avoid mipmap issues
vec2 parallaxOcclusionMapping(vec2 texCoords, vec3 viewDirTangent, uint heightIdx) {
    // Number of layers based on view angle (more layers at grazing angles)
    float numLayers = mix(float(MAX_PARALLAX_LAYERS), float(MIN_PARALLAX_LAYERS),
                          abs(dot(vec3(0.0, 0.0, 1.0), viewDirTangent)));

    // Height of each layer
    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    // Amount to shift texture coordinates per layer
    vec2 P = viewDirTangent.xy / viewDirTangent.z * HEIGHT_SCALE;
    vec2 deltaTexCoords = P / numLayers;

    // Current texture coordinates and height
    // Use textureLod with LOD 0 inside loop - derivatives are unreliable in loops
    vec2 currentTexCoords = texCoords;
    float currentDepthMapValue = 1.0 - textureLod(bindlessTextures[nonuniformEXT(heightIdx)], currentTexCoords, 0.0).r;

    // Raymarching through height layers
    while (currentLayerDepth < currentDepthMapValue) {
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = 1.0 - textureLod(bindlessTextures[nonuniformEXT(heightIdx)], currentTexCoords, 0.0).r;
        currentLayerDepth += layerDepth;
    }

    // Get texture coordinates before collision (for interpolation)
    vec2 prevTexCoords = currentTexCoords + deltaTexCoords;

    // Get depth values before and after collision for linear interpolation
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = (1.0 - textureLod(bindlessTextures[nonuniformEXT(heightIdx)], prevTexCoords, 0.0).r)
                        - currentLayerDepth + layerDepth;

    // Interpolate texture coordinates
    float weight = afterDepth / (afterDepth - beforeDepth);
    vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight);

    return finalTexCoords;
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
    uint heightIdx = drawData.textureIndices1.w;

    // Construct TBN matrix from screen-space derivatives (needed for parallax and normal mapping)
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
    mat3 TBN = mat3(T, B, N);

    // Calculate texture coordinate derivatives for proper mipmap selection
    // These must be computed before any control flow that modifies texCoords
    vec2 texDx = dFdx(fragTexCoord);
    vec2 texDy = dFdy(fragTexCoord);

    // Calculate texture coordinates (apply parallax mapping if height texture available)
    vec2 texCoords = fragTexCoord;
    bool useParallax = isValidTexture(heightIdx);

    if (useParallax) {
        // Transform view direction to tangent space for parallax mapping
        mat3 invTBN = transpose(TBN);  // TBN is orthonormal, so transpose = inverse
        vec3 viewDirTangent = normalize(invTBN * V);

        // Apply parallax occlusion mapping
        texCoords = parallaxOcclusionMapping(fragTexCoord, viewDirTangent, heightIdx);

        // Discard fragments outside texture bounds (prevents edge artifacts)
        if (texCoords.x < 0.0 || texCoords.x > 1.0 || texCoords.y < 0.0 || texCoords.y > 1.0) {
            discard;
        }
    }

    // Sample albedo using parallax-adjusted coordinates
    // Use textureGrad with original derivatives for correct mipmap selection
    vec3 albedo = drawData.albedo.rgb;
    float alpha = drawData.albedo.a;

    if (isValidTexture(albedoIdx)) {
        vec4 albedoSample = textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], texCoords, texDx, texDy);
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
        vec3 ormValues = unpackORM(textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], texCoords, texDx, texDy));
        ao = ormValues.x;
        roughness = ormValues.y;
        metallic = ormValues.z;
    } else {
        // Fallback to individual textures
        if (isValidTexture(metallicIdx)) {
            metallic = textureGrad(bindlessTextures[nonuniformEXT(metallicIdx)], texCoords, texDx, texDy).r;
        }
        if (isValidTexture(roughnessIdx)) {
            roughness = textureGrad(bindlessTextures[nonuniformEXT(roughnessIdx)], texCoords, texDx, texDy).r;
        }
        if (isValidTexture(aoIdx)) {
            ao = textureGrad(bindlessTextures[nonuniformEXT(aoIdx)], texCoords, texDx, texDy).r;
        }
    }

    // Normal mapping using pre-computed TBN matrix
    if (isValidTexture(normalIdx)) {
        vec3 tangentNormal = textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], texCoords, texDx, texDy).rgb * 2.0 - 1.0;
        // Transform normal from tangent space to world space
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

    // Add emission (using parallax-adjusted coordinates with proper mipmap selection)
    vec3 emissive = vec3(0.0);
    if (isValidTexture(emissionIdx)) {
        emissive = pow(textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], texCoords, texDx, texDy).rgb, vec3(2.2)) * emission;
    } else {
        emissive = albedo * emission;
    }

    vec3 color = ambient + emissive;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    // Debug: visualize LOD levels with colors
    if (DEBUG_VISUALIZE_LOD != 0) {
        vec3 lodColors[4] = vec3[4](
            vec3(0.0, 1.0, 0.0),   // LOD0: Green
            vec3(1.0, 1.0, 0.0),   // LOD1: Yellow
            vec3(1.0, 0.5, 0.0),   // LOD2: Orange
            vec3(1.0, 0.0, 0.0)    // LOD3: Red
        );
        uint lod = min(drawData.lodLevel, 3u);
        color = mix(color, lodColors[lod], 0.5);
    }

    outColor = vec4(color, alpha);
}
