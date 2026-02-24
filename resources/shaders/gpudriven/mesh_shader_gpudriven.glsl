#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"

const uint MESHLET_MAX_VERTICES = 64;
const uint MESHLET_MAX_PRIMITIVES = 124;

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(location = 0) out vec3 fragWorldPos[];
layout(location = 1) out vec3 fragNormal[];
layout(location = 2) out vec2 fragTexCoord[];
layout(location = 3) flat out uint fragDrawIndex[];
layout(location = 4) flat out uint fragMeshletIndex[];

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

layout(std430, set = 3, binding = 1) readonly buffer MeshletVertexBuffer {
    uint meshletVertices[];
};

layout(std430, set = 3, binding = 2) readonly buffer MeshletPrimitiveBuffer {
    uint meshletPrimitives[];
};

layout(std430, set = 4, binding = 0) readonly buffer VertexBuffer {
    float vertexData[];
};

layout(std430, set = 5, binding = 0) readonly buffer BoneMatrices {
    mat4 boneMatrices[];
};

const uint MAX_MESHLETS_PER_PAYLOAD = 32;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
};

taskPayloadSharedEXT MeshletPayload payload;

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
} pc;

shared vec3 sharedPositions[MESHLET_MAX_VERTICES];
shared vec3 sharedNormals[MESHLET_MAX_VERTICES];
shared vec2 sharedTexCoords[MESHLET_MAX_VERTICES];

uvec3 unpackPrimitive(uint packed) {
    return uvec3(
        packed & 0xFFu,
        (packed >> 8) & 0xFFu,
        (packed >> 16) & 0xFFu
    );
}

void main() {
    uint payloadMeshletIndex = gl_WorkGroupID.x;
    if (payloadMeshletIndex >= payload.meshletCount) {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    uint globalMeshletIndex = payload.meshletIndices[payloadMeshletIndex];
    uint drawIndex = payload.drawIndex;
    GPUMeshlet meshlet = meshlets[globalMeshletIndex];
    PerDrawData drawData = perDrawData[drawIndex];

    uint vertexCount, primitiveCount;
    unpackMeshletCounts(meshlet.vertexPrimCount, vertexCount, primitiveCount);
    SetMeshOutputsEXT(vertexCount, primitiveCount);

    mat4 modelMatrix = drawData.modelMatrix;
    mat3 normalMatrix = mat3(drawData.normalMatrix);
    mat4 viewProjection = camera.projection * camera.view;

    uint numIterations = (vertexCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            uint meshletLocalVertexIdx = meshletVertices[meshlet.vertexOffset + localVertexIndex];
            uint globalVertexIndex = meshlet.globalVertexOffset + meshletLocalVertexIdx;
            uint baseIdx = globalVertexIndex * 16;

            vec3 position = vec3(
                vertexData[baseIdx + 0],
                vertexData[baseIdx + 1],
                vertexData[baseIdx + 2]
            );
            vec3 normal = vec3(
                vertexData[baseIdx + 3],
                vertexData[baseIdx + 4],
                vertexData[baseIdx + 5]
            );

            if (drawData.boneMatrixOffset != 0xFFFFFFFFu) {
                ivec4 boneIndices = ivec4(
                    floatBitsToInt(vertexData[baseIdx + 8]),
                    floatBitsToInt(vertexData[baseIdx + 9]),
                    floatBitsToInt(vertexData[baseIdx + 10]),
                    floatBitsToInt(vertexData[baseIdx + 11])
                );
                vec4 boneWeights = vec4(
                    vertexData[baseIdx + 12],
                    vertexData[baseIdx + 13],
                    vertexData[baseIdx + 14],
                    vertexData[baseIdx + 15]
                );

                mat4 skinMatrix = mat4(0.0);
                float totalWeight = 0.0;
                for (int i = 0; i < 4; ++i) {
                    int boneIdx = boneIndices[i];
                    float weight = boneWeights[i];
                    if (boneIdx >= 0 && weight > 0.0) {
                        uint globalBoneIdx = drawData.boneMatrixOffset + uint(boneIdx);
                        skinMatrix += boneMatrices[globalBoneIdx] * weight;
                        totalWeight += weight;
                    }
                }

                if (totalWeight > 0.0) {
                    position = (skinMatrix * vec4(position, 1.0)).xyz;
                    normal = normalize(mat3(skinMatrix) * normal);
                }
            }

            sharedPositions[localVertexIndex] = position;
            sharedNormals[localVertexIndex] = normal;
            sharedTexCoords[localVertexIndex] = vec2(
                vertexData[baseIdx + 6],
                vertexData[baseIdx + 7]
            );
        }
    }

    barrier();

    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            vec4 worldPos = modelMatrix * vec4(sharedPositions[localVertexIndex], 1.0);
            fragWorldPos[localVertexIndex] = worldPos.xyz;
            fragNormal[localVertexIndex] = normalize(normalMatrix * sharedNormals[localVertexIndex]);
            fragTexCoord[localVertexIndex] = sharedTexCoords[localVertexIndex];
            fragDrawIndex[localVertexIndex] = drawIndex;
            fragMeshletIndex[localVertexIndex] = globalMeshletIndex;
            gl_MeshVerticesEXT[localVertexIndex].gl_Position = viewProjection * worldPos;
        }
    }

    uint numPrimIterations = (primitiveCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
    for (uint iter = 0; iter < numPrimIterations; iter++) {
        uint localPrimIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localPrimIndex < primitiveCount) {
            uint packedPrimitive = meshletPrimitives[meshlet.primitiveOffset + localPrimIndex];
            uvec3 indices = unpackPrimitive(packedPrimitive);
            gl_PrimitiveTriangleIndicesEXT[localPrimIndex] = indices;
        }
    }
}

#type FRAGMENT
#version 460 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in flat uint fragDrawIndex;
layout(location = 4) in flat uint fragMeshletIndex;

layout(location = 0) out vec4 outColor;
#ifdef WBOIT_ENABLED
layout(location = 1) out float outRevealage;
#endif

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
} pc;

// Matches GPUDirectionalLight in GPULightTypes.hpp (32 bytes)
struct DirectionalLight {
    vec3 direction;
    float intensity;
    vec3 color;
    int shadowIndex;
};

layout(std430, set = 6, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight directionalLights[];
};

// Matches GPUPointLight in GPULightTypes.hpp (48 bytes)
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

layout(std430, set = 6, binding = 1) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

// Matches GPUSpotLight in GPULightTypes.hpp (64 bytes)
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

layout(std430, set = 6, binding = 2) readonly buffer SpotLightBuffer {
    SpotLight spotLights[];
};

struct LightCounts {
    uint directionalCount;
    uint pointCount;
    uint spotCount;
    float shadowIntensity;
};

layout(std140, set = 6, binding = 3) uniform LightCountsUBO {
    LightCounts lightCounts;
};

struct ClusterGridParams {
    uvec4 gridDimensions;
    vec4 screenParams;
    vec4 depthParams;
    mat4 invProjection;
    vec4 clusterScale;
    vec4 clusterBias;
};

layout(std140, set = 7, binding = 0) uniform ClusterParamsUBO {
    ClusterGridParams clusterParams;
};

struct ClusterLightData {
    uint offset;
    uint counts;
};

layout(std430, set = 8, binding = 0) readonly buffer ClusterLightGridBuffer {
    ClusterLightData clusterLightGrid[];
};

layout(std430, set = 8, binding = 1) readonly buffer ClusterLightIndexListBuffer {
    uint lightIndexList[];
};

// Matches GPUShadowData in ShadowTypes.hpp (128 bytes)
struct ShadowData {
    mat4 viewProjection;
    vec4 atlasViewport;
    vec4 biasParams;
    vec4 rangeParams;
    vec4 pcfParams;
};

layout(std430, set = 9, binding = 0) readonly buffer ShadowDataBuffer {
    ShadowData shadowData[];
};

layout(set = 10, binding = 0) uniform sampler2DShadow shadowAtlas;
layout(set = 10, binding = 1) uniform sampler2DArrayShadow shadowCascades;
layout(set = 10, binding = 2) uniform samplerCubeShadow shadowCubes[];

// Shadow constants (must match ShadowTypes.hpp)
const int MAX_SHADOW_VIEWS = 272;       // MAX_TOTAL_SHADOW_VIEWS
const int MAX_POINT_SHADOW_CUBES = 32;  // MAX_POINT_SHADOW_CASTERS

const float LIGHTING_PI = 3.14159265359;

const uint LIGHT_INDEX_MASK = 0x7FFFFFFFu;

float linearizeDepth(float windowZ) {
    float near = clusterParams.depthParams.x;
    float far = clusterParams.depthParams.y;
    float denominator = max(far - windowZ * (far - near), 0.0001);
    return near * far / denominator;
}

uint getClusterIndex(vec2 fragCoord, float viewZ) {
    float clampedZ = max(viewZ, clusterParams.depthParams.x);

    uint tileX = uint(fragCoord.x / clusterParams.screenParams.z);
    uint tileY = uint(fragCoord.y / clusterParams.screenParams.w);

    float logRatio = log(clampedZ / clusterParams.depthParams.x);
    uint slice = uint(logRatio * clusterParams.depthParams.w);

    tileX = min(tileX, clusterParams.gridDimensions.x - 1u);
    tileY = min(tileY, clusterParams.gridDimensions.y - 1u);
    slice = min(slice, clusterParams.gridDimensions.z - 1u);

    return tileX + tileY * clusterParams.gridDimensions.x +
           slice * clusterParams.gridDimensions.x * clusterParams.gridDimensions.y;
}

float smoothDistanceAttenuation(float distance, float range) {
    float distRatio = distance / range;
    float attenuation = clamp(1.0 - distRatio * distRatio, 0.0, 1.0);
    return attenuation * attenuation;
}

const float LIGHT_INTENSITY_SCALE = 100.0;

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

float sampleSpotShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    // Bounds validation to prevent GPU crash from invalid indices
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = shadowData[shadowIndex];

    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z;
    vec4 lightSpacePos = sd.viewProjection * vec4(biasedPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    projCoords.xy = sd.atlasViewport.xy + projCoords.xy * sd.atlasViewport.zw;

    if (projCoords.z > 1.0 || projCoords.z < 0.0) return 1.0;
    if (any(lessThan(projCoords.xy, vec2(0.0))) || any(greaterThan(projCoords.xy, vec2(1.0)))) return 1.0;

    bool filterEnabled = sd.pcfParams.z > 0.5;
    int kernelSize = int(sd.pcfParams.x);

    if (!filterEnabled || kernelSize == 0) {
        return texture(shadowAtlas, vec3(projCoords.xy, projCoords.z));
    }

    float shadow = 0.0;
    float texelSize = sd.biasParams.w;
    float softness = sd.pcfParams.y;
    float spread = texelSize * softness;
    int sampleCount = 0;
    int size = kernelSize + 1;  // 3x3 or 5x5
    float halfSize = float(size) * 0.5;

    for (int x = 0; x < size; ++x) {
        for (int y = 0; y < size; ++y) {
            vec2 offset = (vec2(float(x), float(y)) - halfSize + 0.5) * spread;
            shadow += texture(shadowAtlas, vec3(projCoords.xy + offset, projCoords.z));
            sampleCount++;
        }
    }
    return shadow / float(sampleCount);
}

float sampleCascadeShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    // Bounds validation to prevent GPU crash from invalid indices
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = shadowData[shadowIndex];

    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z;
    vec4 lightSpacePos = sd.viewProjection * vec4(biasedPos, 1.0);

    if (lightSpacePos.w <= 0.0) return 1.0;

    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;
    texCoords = clamp(texCoords, 0.0, 1.0);
    projCoords.xy = sd.atlasViewport.xy + texCoords * sd.atlasViewport.zw;
    projCoords.z = clamp(projCoords.z, 0.0, 1.0);

    bool filterEnabled = sd.pcfParams.z > 0.5;
    int kernelSize = int(sd.pcfParams.x);

    if (!filterEnabled || kernelSize == 0) {
        return texture(shadowAtlas, vec3(projCoords.xy, projCoords.z));
    }

    float shadow = 0.0;
    float spread = sd.biasParams.w * sd.pcfParams.y;
    int sampleCount = 0;
    int size = kernelSize + 1;  // 3x3 or 5x5
    float halfSize = float(size) * 0.5;

    for (int x = 0; x < size; ++x) {
        for (int y = 0; y < size; ++y) {
            vec2 offset = (vec2(float(x), float(y)) - halfSize + 0.5) * spread;
            shadow += texture(shadowAtlas, vec3(projCoords.xy + offset, projCoords.z));
            sampleCount++;
        }
    }
    return shadow / float(sampleCount);
}

float sampleDirectionalShadow(int baseShadowIndex, vec3 worldPos, vec3 worldNormal, float viewZ) {
    // Bounds validation to prevent GPU crash from invalid indices
    if (baseShadowIndex < 0 || baseShadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    int cascadeCount = int(shadowData[baseShadowIndex].rangeParams.z);
    cascadeCount = clamp(cascadeCount, 1, 4);

    // Ensure we don't access beyond buffer bounds with cascades
    if (baseShadowIndex + cascadeCount > MAX_SHADOW_VIEWS) {
        cascadeCount = MAX_SHADOW_VIEWS - baseShadowIndex;
        if (cascadeCount <= 0) return 1.0;
    }

    int cascadeIdx = 0;
    for (int i = 0; i < cascadeCount; ++i) {
        if (viewZ < shadowData[baseShadowIndex + i].rangeParams.y) {
            cascadeIdx = i;
            break;
        }
        cascadeIdx = i;
    }

    int shadowIndex = baseShadowIndex + cascadeIdx;
    float cascadeFar = shadowData[shadowIndex].rangeParams.y;

    float shadow = sampleCascadeShadow(shadowIndex, worldPos, worldNormal);

    float blendZoneStart = cascadeFar * 0.9;
    if (viewZ > blendZoneStart && cascadeIdx < cascadeCount - 1) {
        float nextShadow = sampleCascadeShadow(shadowIndex + 1, worldPos, worldNormal);
        float blendFactor = smoothstep(blendZoneStart, cascadeFar, viewZ);
        shadow = mix(shadow, nextShadow, blendFactor);
    }

    float maxDistance = shadowData[baseShadowIndex + cascadeCount - 1].rangeParams.y;
    float fadeStart = maxDistance * 0.85;
    float fadeFactor = 1.0 - smoothstep(fadeStart, maxDistance, viewZ);

    return mix(1.0, shadow, fadeFactor);
}

float samplePointShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal, vec3 lightPos, float lightRadius) {
    // Bounds validation to prevent GPU crash from invalid indices
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = shadowData[shadowIndex];

    int cubeMapIndex = int(sd.pcfParams.w);
    if (cubeMapIndex < 0 || cubeMapIndex >= MAX_POINT_SHADOW_CUBES) return 1.0;

    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z;
    vec3 lightToFrag = biasedPos - lightPos;
    float linearDepth = length(lightToFrag);
    vec3 sampleDir = normalize(lightToFrag);

    float near = sd.rangeParams.x;
    float far = sd.rangeParams.y;

    float majorComponent = max(abs(sampleDir.x), max(abs(sampleDir.y), abs(sampleDir.z)));
    float viewSpaceZ = linearDepth * majorComponent;
    float perspectiveDepth = (far * (viewSpaceZ - near)) / (viewSpaceZ * (far - near));

    bool filterEnabled = sd.pcfParams.z > 0.5;
    int kernelSize = int(sd.pcfParams.x);

    if (!filterEnabled || kernelSize == 0) {
        return texture(shadowCubes[nonuniformEXT(cubeMapIndex)], vec4(sampleDir, perspectiveDepth));
    }

    float softness = sd.pcfParams.y;
    float spread = softness * 0.01;

    vec3 tangent = abs(sampleDir.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 bitangent = normalize(cross(sampleDir, tangent));
    tangent = normalize(cross(bitangent, sampleDir));

    float shadow = 0.0;
    int sampleCount = 0;
    int size = kernelSize + 1;  // 3x3 or 5x5
    float halfSize = float(size) * 0.5;

    for (int x = 0; x < size; ++x) {
        for (int y = 0; y < size; ++y) {
            float fx = float(x) - halfSize + 0.5;
            float fy = float(y) - halfSize + 0.5;
            vec3 offset = tangent * fx * spread + bitangent * fy * spread;
            vec3 offsetDir = normalize(sampleDir + offset);
            shadow += texture(shadowCubes[nonuniformEXT(cubeMapIndex)], vec4(offsetDir, perspectiveDepth));
            sampleCount++;
        }
    }
    return shadow / float(sampleCount);
}

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

const float ALPHA_CUTOFF = 0.5;
const float MAX_REFLECTION_LOD = 4.0;
const uint INVALID_TEXTURE_INDEX = 0xFFFFFFFF;
const uint FLAG_ALPHA_MASK = 1u << 4;
const uint FLAG_TRANSLUCENT = 1u << 5;
const uint FLAG_ADDITIVE_BLEND = 1u << 10;
const uint FLAG_MULTIPLY_BLEND = 1u << 11;

bool isValidTexture(uint index) {
    return index != INVALID_TEXTURE_INDEX && index != 0xFFu && index < 4096u;
}

vec3 unpackORM(vec4 ormSample) {
    return vec3(ormSample.r, ormSample.g, ormSample.b);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    PerDrawData drawData = perDrawData[fragDrawIndex];

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    uint albedoIdx = drawData.textureIndices0.x;
    uint normalIdx = drawData.textureIndices0.y;
    uint ormIdx = drawData.textureIndices0.z;
    uint metallicIdx = drawData.textureIndices0.w;
    uint roughnessIdx = drawData.textureIndices1.x;
    uint aoIdx = drawData.textureIndices1.y;
    uint emissionIdx = drawData.textureIndices1.z;

    vec2 texCoords = fragTexCoord;
    if (drawData.shaderGroupIndex == 1u) {
        texCoords += vec2(camera.time * 0.1, 0.0);
    }

    vec2 texDx = dFdx(fragTexCoord);
    vec2 texDy = dFdy(fragTexCoord);

    vec3 albedo = drawData.albedo.rgb;
    float alpha = drawData.albedo.a;

    if (isValidTexture(albedoIdx)) {
        vec4 albedoSample = textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], texCoords, texDx, texDy);
        albedo = pow(albedoSample.rgb, vec3(2.2));
        alpha = albedoSample.a;
    }

    if ((drawData.flags & FLAG_ALPHA_MASK) != 0u) {
        if (alpha < ALPHA_CUTOFF) {
            discard;
        }
    }

    if ((drawData.flags & FLAG_TRANSLUCENT) != 0u) {
        // Unpack opacity from blendModeAndOpacity: bits 16-31 store opacity as uint16 (0-65535 -> 0.0-1.0)
        float materialOpacity = float(drawData.blendModeAndOpacity >> 16u) / 65535.0;
        alpha *= materialOpacity;
    }

    float metallic = drawData.materialParams.x;
    float roughness = drawData.materialParams.y;
    float ao = drawData.materialParams.z;
    float emission = drawData.materialParams.w;

    if (isValidTexture(ormIdx)) {
        vec3 ormValues = unpackORM(textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], texCoords, texDx, texDy));
        ao = ormValues.x;
        roughness = ormValues.y;
        metallic = ormValues.z;
    } else {
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

    if (isValidTexture(normalIdx)) {
        vec3 pos_dx = dFdx(fragWorldPos);
        vec3 pos_dy = dFdy(fragWorldPos);
        vec2 uv_dx = dFdx(fragTexCoord);
        vec2 uv_dy = dFdy(fragTexCoord);

        vec3 T = normalize(pos_dx * uv_dy.y - pos_dy * uv_dx.y);
        vec3 B = normalize(pos_dy * uv_dx.x - pos_dx * uv_dy.x);
        T = normalize(T - N * dot(N, T));
        B = cross(N, T);
        mat3 TBN = mat3(T, B, N);

        vec3 tangentNormal = textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], texCoords, texDx, texDy).rgb * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }

    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo * drawData.iblDiffuse;

    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y) * drawData.iblSpecular;

    vec3 ambient = (kD * diffuse + specular) * ao;

    vec3 directLighting = vec3(0.0);
    float minShadow = 1.0;

    float linearZ = linearizeDepth(gl_FragCoord.z);

    if (lightCounts.pointCount > 0u || lightCounts.spotCount > 0u) {
        uint clusterIdx = getClusterIndex(gl_FragCoord.xy, linearZ);

        ClusterLightData clusterData = clusterLightGrid[clusterIdx];
        uint clusterPointCount = clusterData.counts & 0xFFFFu;
        uint clusterSpotCount = clusterData.counts >> 16u;
        uint lightOffset = clusterData.offset;

        for (uint i = 0u; i < clusterPointCount; ++i) {
            uint lightIdx = lightIndexList[lightOffset + i];
            PointLight light = pointLights[lightIdx];
            float shadow = samplePointShadow(light.shadowIndex, fragWorldPos, N, light.position, light.radius);
            minShadow = min(minShadow, shadow);
            directLighting += evaluatePointLight(fragWorldPos, N, V, albedo, metallic, roughness, F0, light) * shadow;
        }

        for (uint i = 0u; i < clusterSpotCount; ++i) {
            uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
            uint lightIdx = packedIdx & LIGHT_INDEX_MASK;
            SpotLight light = spotLights[lightIdx];
            float shadow = sampleSpotShadow(light.shadowIndex, fragWorldPos, N);
            minShadow = min(minShadow, shadow);
            directLighting += evaluateSpotLight(fragWorldPos, N, V, albedo, metallic, roughness, F0, light) * shadow;
        }
    }

    for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];
        float shadow = sampleDirectionalShadow(light.shadowIndex, fragWorldPos, N, linearZ);
        minShadow = min(minShadow, shadow);
        directLighting += evaluateDirectionalLight(N, V, albedo, metallic, roughness, F0, light) * shadow;
    }

    float shadowContrast = 1.0 + lightCounts.shadowIntensity * 2.0;
    float adjustedShadow = pow(minShadow, shadowContrast);
    float ambientShadowFactor = mix(1.0, adjustedShadow, lightCounts.shadowIntensity);
    ambient *= ambientShadowFactor;

    float emissionMultiplier = emission;
    if (drawData.shaderGroupIndex == 2u) {
        emissionMultiplier *= cos(camera.time);
    }

    vec3 emissive = vec3(0.0);
    if (isValidTexture(emissionIdx)) {
        emissive = pow(textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], texCoords, texDx, texDy).rgb, vec3(2.2)) * emissionMultiplier;
    } else {
        emissive = albedo * emissionMultiplier;
    }

    vec3 color = ambient + directLighting + emissive;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    uint viewModeValue = pc.viewMode & 0xFFu;

    if (viewModeValue == 1u) {
        uint h = fragMeshletIndex;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = (h >> 16) ^ h;

        vec3 meshletColor = vec3(
            float((h >> 0) & 0xFFu) / 255.0,
            float((h >> 8) & 0xFFu) / 255.0,
            float((h >> 16) & 0xFFu) / 255.0
        );
        meshletColor = normalize(meshletColor + 0.1) * 0.8;
        color = meshletColor;
    }

    if (viewModeValue == 2u) {
        vec3 lodColors[4] = vec3[4](
            vec3(0.0, 1.0, 0.0),
            vec3(1.0, 1.0, 0.0),
            vec3(1.0, 0.5, 0.0),
            vec3(1.0, 0.0, 0.0)
        );
        uint lod = min(drawData.lodLevel, 3u);
        color = mix(color, lodColors[lod], 0.5);
    }

    if (viewModeValue == 3u) {
        float dx = max(length(texDx), length(texDy));
        float mipLevel = log2(max(dx * 1024.0, 1.0));
        mipLevel = clamp(mipLevel, 0.0, 10.0);

        vec3 mipColors[5] = vec3[5](
            vec3(0.0, 0.0, 1.0),
            vec3(0.0, 1.0, 1.0),
            vec3(0.0, 1.0, 0.0),
            vec3(1.0, 1.0, 0.0),
            vec3(1.0, 0.0, 0.0)
        );

        float t = mipLevel / 2.0;
        int idx = clamp(int(floor(t)), 0, 3);
        vec3 mipColor = mix(mipColors[idx], mipColors[idx + 1], fract(t));
        color = mipColor;
    }

    if (viewModeValue == 4u) {
        float linearZ = linearizeDepth(gl_FragCoord.z);
        uint clusterIdx = getClusterIndex(gl_FragCoord.xy, linearZ);

        uint h = clusterIdx;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = (h >> 16) ^ h;

        vec3 clusterColor = vec3(
            float((h >> 0) & 0xFFu) / 255.0,
            float((h >> 8) & 0xFFu) / 255.0,
            float((h >> 16) & 0xFFu) / 255.0
        );
        clusterColor = normalize(clusterColor + 0.1) * 0.8;
        color = clusterColor;
    }

    if (viewModeValue == 5u) {
        float linearZ = linearizeDepth(gl_FragCoord.z);
        float near = clusterParams.depthParams.x;
        float far = clusterParams.depthParams.y;
        float normalizedDepth = clamp((linearZ - near) / (far - near), 0.0, 1.0);

        vec3 depthColors[5] = vec3[5](
            vec3(0.0, 0.0, 1.0),
            vec3(0.0, 1.0, 1.0),
            vec3(0.0, 1.0, 0.0),
            vec3(1.0, 1.0, 0.0),
            vec3(1.0, 0.0, 0.0)
        );
        float t = normalizedDepth * 4.0;
        int idx = clamp(int(floor(t)), 0, 3);
        color = mix(depthColors[idx], depthColors[idx + 1], fract(t));
    }

    if (viewModeValue == 6u) {
        float totalShadow = 1.0;

        for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
            DirectionalLight light = directionalLights[i];
            float shadow = sampleDirectionalShadow(light.shadowIndex, fragWorldPos, N, linearZ);
            totalShadow = min(totalShadow, shadow);
        }

        if (lightCounts.pointCount > 0u || lightCounts.spotCount > 0u) {
            uint clusterIdx = getClusterIndex(gl_FragCoord.xy, linearZ);
            ClusterLightData clusterData = clusterLightGrid[clusterIdx];
            uint clusterPointCount = clusterData.counts & 0xFFFFu;
            uint clusterSpotCount = clusterData.counts >> 16u;
            uint lightOffset = clusterData.offset;

            for (uint i = 0u; i < clusterPointCount; ++i) {
                uint packedIdx = lightIndexList[lightOffset + i];
                uint lightIdx = packedIdx & LIGHT_INDEX_MASK;
                PointLight light = pointLights[lightIdx];
                if (light.shadowIndex >= 0) {
                    float shadow = samplePointShadow(light.shadowIndex, fragWorldPos, N, light.position, light.radius);
                    totalShadow = min(totalShadow, shadow);
                }
            }

            for (uint i = 0u; i < clusterSpotCount; ++i) {
                uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
                uint lightIdx = packedIdx & LIGHT_INDEX_MASK;
                SpotLight light = spotLights[lightIdx];
                if (light.shadowIndex >= 0) {
                    float shadow = sampleSpotShadow(light.shadowIndex, fragWorldPos, N);
                    totalShadow = min(totalShadow, shadow);
                }
            }
        }

        vec3 shadowColor = mix(vec3(0.1, 0.1, 0.3), vec3(1.0, 0.95, 0.9), totalShadow);
        color = shadowColor;
    }

#ifdef WBOIT_ENABLED
    // Weighted Blended OIT (McGuire & Bavoil 2013)
    float z = gl_FragCoord.z;
    float w = alpha * max(1e-2, min(3e3, 10.0 / (1e-5 + pow(z / 200.0, 4.0))));
    outColor = vec4(color * alpha * w, alpha * w);
    outRevealage = alpha;
#else
    outColor = vec4(color, alpha);
#endif
}
