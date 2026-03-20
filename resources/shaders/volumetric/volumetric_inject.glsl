#type COMPUTE
#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant) uniform PushConstants {
    uint frameIndex;
} pc;

// Set 0: Volumetric Grid (VolumetricGridManager)
layout(std140, set = 0, binding = 0) uniform VolumetricParamsUBO {
    uvec4 gridDimensions;       // xyz = width, height, depth
    vec4 depthParams;            // x = near, y = far, z = log(far/near), w = 1/log(far/near)
    mat4 invViewProjection;
    mat4 prevViewProjection;
    vec4 fogParams;              // x = uniformDensity, y = heightFogDensity, z = heightFogFalloff, w = heightFogOffset
    vec4 scatterParams;          // x = scatteringCoeff, y = absorptionCoeff, z = anisotropy, w = maxDistance
    vec4 fogColor;               // rgb = fog color, a = intensity
    vec4 ambientParams;          // x = ambientIntensity, y = temporalBlendFactor, z = frameIndex, w = unused
    vec4 cameraPosition;         // xyz = world pos
};

layout(rgba16f, set = 0, binding = 1) uniform writeonly image3D scatteringVolume;

// Set 1: Cluster Grid Data
struct ClusterGridParams {
    uvec4 gridDimensions;
    vec4 screenParams;
    vec4 depthParams;
    mat4 invProjection;
    vec4 clusterScale;
    vec4 clusterBias;
};

layout(std140, set = 1, binding = 0) uniform ClusterParamsUBO {
    ClusterGridParams clusterParams;
};

layout(std430, set = 1, binding = 1) readonly buffer ClusterAABBBuffer {
    vec4 clusterAABBs[]; // pairs of min/max
};

// Set 2: Light Data
struct DirectionalLight {
    vec3 direction;
    float intensity;
    vec3 color;
    int shadowIndex;
};

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

layout(std430, set = 2, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight directionalLights[];
};

layout(std430, set = 2, binding = 1) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

layout(std430, set = 2, binding = 2) readonly buffer SpotLightBuffer {
    SpotLight spotLights[];
};

struct LightCounts {
    uint directionalCount;
    uint pointCount;
    uint spotCount;
    float shadowIntensity;
};

layout(std140, set = 2, binding = 3) uniform LightCountsUBO {
    LightCounts lightCounts;
};

// Set 3: Light Culling Output
struct ClusterLightData {
    uint offset;
    uint counts;
};

layout(std430, set = 3, binding = 0) readonly buffer ClusterLightGridBuffer {
    ClusterLightData clusterLightGrid[];
};

layout(std430, set = 3, binding = 1) readonly buffer ClusterLightIndexListBuffer {
    uint lightIndexList[];
};

// Set 4: Shadow Data
#include "../common/shadow_sampling_types.glsl"

layout(std430, set = 4, binding = 0) readonly buffer ShadowDataBuffer {
    ShadowData shadowData[];
};

layout(std430, set = 4, binding = 1) readonly buffer PageTableBuffer {
    uint pageTableVol[];
};

// Set 5: Shadow Textures
layout(set = 5, binding = 0) uniform sampler2DShadow physicalPoolShadow;
layout(set = 5, binding = 1) uniform sampler2D physicalPoolDepth;
layout(set = 5, binding = 2) uniform samplerCubeShadow shadowCubes[32];
layout(set = 5, binding = 3) uniform samplerCube shadowCubesDepth[32];

// VSM Page Table constants and lookup
#define SHADOW_BUFFER shadowData
#define PAGE_TABLE pageTableVol

const uint PAGE_SIZE = 128u;
const uint PHYSICAL_POOL_DIM = 8192u;
const uint PAGE_ENTRY_VALID_BIT = 0x80000000u;
const uint PAGE_ENTRY_X_MASK = 0x3Fu;
const uint PAGE_ENTRY_Y_SHIFT = 6u;
const uint PAGE_ENTRY_Y_MASK = 0x3Fu;
const float POOL_DIM_F = float(PHYSICAL_POOL_DIM);
const float PAGE_SIZE_F = float(PAGE_SIZE);

// Shadow constants (must match ShadowTypes.hpp)
const int MAX_SHADOW_VIEWS = 272;

const float VOL_PI = 3.14159265359;
const float LIGHT_INTENSITY_SCALE = 100.0;
const uint LIGHT_INDEX_MASK = 0x7FFFFFFFu;

float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * VOL_PI * pow(denom, 1.5));
}

float sliceToDepth(float slice, float near, float far, float numSlices) {
    float t = slice / numSlices;
    return near * pow(far / near, t);
}

vec3 froxelToWorld(ivec3 froxelCoord, uvec3 dims) {
    vec2 uv = (vec2(froxelCoord.xy) + 0.5) / vec2(dims.xy);
    float near = depthParams.x;
    float far = depthParams.y;
    float depth = sliceToDepth(float(froxelCoord.z) + 0.5, near, far, float(dims.z));

    vec2 ndc = uv * 2.0 - 1.0;
    float ndcDepth = (far * (depth - near)) / (depth * (far - near));

    vec4 clipPos = vec4(ndc, ndcDepth, 1.0);
    vec4 worldPos = invViewProjection * clipPos;
    return worldPos.xyz / worldPos.w;
}

float computeFogDensity(vec3 worldPos) {
    float density = fogParams.x;

    // Height-based exponential fog
    float heightAboveOffset = worldPos.y - fogParams.w;
    float heightFalloff = fogParams.z;
    if (heightFalloff > 0.001) {
        density += fogParams.y * exp(-heightFalloff * max(heightAboveOffset, 0.0));
    } else {
        density += fogParams.y;
    }

    return max(density, 0.0);
}

uint froxelToClusterIndex(ivec3 froxelCoord, uvec3 volDims) {
    float screenX = (float(froxelCoord.x) + 0.5) / float(volDims.x) * clusterParams.screenParams.x;
    float screenY = (float(froxelCoord.y) + 0.5) / float(volDims.y) * clusterParams.screenParams.y;

    uint tileX = uint(screenX / clusterParams.screenParams.z);
    uint tileY = uint(screenY / clusterParams.screenParams.w);

    float near = depthParams.x;
    float far = depthParams.y;
    float depth = sliceToDepth(float(froxelCoord.z) + 0.5, near, far, float(volDims.z));

    float clusterNear = clusterParams.depthParams.x;
    float logRatio = log(max(depth, clusterNear) / clusterNear);
    uint slice = uint(logRatio * clusterParams.depthParams.w);

    tileX = min(tileX, clusterParams.gridDimensions.x - 1u);
    tileY = min(tileY, clusterParams.gridDimensions.y - 1u);
    slice = min(slice, clusterParams.gridDimensions.z - 1u);

    return tileX + tileY * clusterParams.gridDimensions.x +
           slice * clusterParams.gridDimensions.x * clusterParams.gridDimensions.y;
}

vec2 vsmLookupPhysicalUVVol(ShadowData sd, vec2 uv, out bool valid) {
    ivec2 pageCoord = ivec2(uv * vec2(sd.pageTableInfo.xy));
    pageCoord = clamp(pageCoord, ivec2(0), sd.pageTableInfo.xy - 1);

    uint entryIdx = uint(sd.pageTableInfo.z) + uint(pageCoord.y * sd.pageTableInfo.x + pageCoord.x);
    uint pageEntry = PAGE_TABLE[entryIdx];

    if ((pageEntry & PAGE_ENTRY_VALID_BIT) == 0u) {
        valid = false;
        return vec2(0.0);
    }

    valid = true;

    uint tileX = pageEntry & PAGE_ENTRY_X_MASK;
    uint tileY = (pageEntry >> PAGE_ENTRY_Y_SHIFT) & PAGE_ENTRY_Y_MASK;

    vec2 pageUV = fract(uv * vec2(sd.pageTableInfo.xy));
    vec2 physicalUV = (vec2(float(tileX), float(tileY)) + pageUV) * (PAGE_SIZE_F / POOL_DIM_F);
    return physicalUV;
}

float sampleCascadeShadowSimple(int shadowIndex, vec3 worldPos) {
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = shadowData[shadowIndex];

    vec4 lightSpacePos = sd.viewProjection * vec4(worldPos, 1.0);
    if (lightSpacePos.w <= 0.0) return 1.0;

    vec3 ndc = lightSpacePos.xyz / lightSpacePos.w;
    if (any(greaterThan(abs(ndc.xy), vec2(1.0)))) return 1.0;

    vec2 uv = ndc.xy * 0.5 + 0.5;
    float receiverDepth = clamp(ndc.z, 0.0, 1.0);

    bool valid;
    vec2 physicalUV = vsmLookupPhysicalUVVol(sd, uv, valid);
    if (!valid) return 1.0;

    return texture(physicalPoolShadow, vec3(physicalUV, receiverDepth));
}

float sampleDirectionalShadowVolumetricCSM(int baseShadowIndex, vec3 worldPos, float viewZ) {
    if (baseShadowIndex < 0 || baseShadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    int cascadeCount = int(shadowData[baseShadowIndex].rangeParams.z);
    cascadeCount = clamp(cascadeCount, 1, 4);

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
    float shadow = sampleCascadeShadowSimple(shadowIndex, worldPos);

    // Fade shadow at max cascade distance
    float maxDistance = shadowData[baseShadowIndex + cascadeCount - 1].rangeParams.y;
    float fadeStart = maxDistance * 0.85;
    float fadeFactor = 1.0 - smoothstep(fadeStart, maxDistance, viewZ);

    return mix(1.0, shadow, fadeFactor);
}

float sampleDirectionalShadowVolumetricClipmap(int baseShadowIndex, vec3 worldPos, float viewZ) {
    if (baseShadowIndex < 0 || baseShadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    int levelCount = int(shadowData[baseShadowIndex].rangeParams.z);
    levelCount = clamp(levelCount, 1, 16);

    if (baseShadowIndex + levelCount > MAX_SHADOW_VIEWS) {
        levelCount = MAX_SHADOW_VIEWS - baseShadowIndex;
        if (levelCount <= 0) return 1.0;
    }

    float baseExtent = shadowData[baseShadowIndex].rangeParams.x;
    float level = log2(max(viewZ, baseExtent) / baseExtent);
    int levelIdx = clamp(int(level), 0, levelCount - 1);

    int shadowIndex = baseShadowIndex + levelIdx;
    float shadow = sampleCascadeShadowSimple(shadowIndex, worldPos);

    float maxExtent = baseExtent * exp2(float(levelCount - 1));
    float fadeStart = maxExtent * 0.85;
    float fadeFactor = 1.0 - smoothstep(fadeStart, maxExtent, viewZ);

    return mix(1.0, shadow, fadeFactor);
}

float sampleDirectionalShadowVolumetric(int baseShadowIndex, vec3 worldPos, float viewZ) {
    if (baseShadowIndex < 0 || baseShadowIndex >= MAX_SHADOW_VIEWS) return 1.0;
    int lightType = shadowData[baseShadowIndex].pageTableInfo.w;
    if (lightType == 3) {
        return sampleDirectionalShadowVolumetricClipmap(baseShadowIndex, worldPos, viewZ);
    }
    return sampleDirectionalShadowVolumetricCSM(baseShadowIndex, worldPos, viewZ);
}

float smoothDistanceAttenuation(float distance, float range) {
    float distRatio = distance / range;
    float attenuation = clamp(1.0 - distRatio * distRatio, 0.0, 1.0);
    return attenuation * attenuation;
}

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

void main() {
    ivec3 froxelCoord = ivec3(gl_GlobalInvocationID.xyz);
    uvec3 dims = gridDimensions.xyz;

    if (froxelCoord.x >= int(dims.x) || froxelCoord.y >= int(dims.y) || froxelCoord.z >= int(dims.z))
        return;

    vec3 worldPos = froxelToWorld(froxelCoord, dims);
    float distFromCamera = length(worldPos - cameraPosition.xyz);
    if (distFromCamera > scatterParams.w) {
        imageStore(scatteringVolume, froxelCoord, vec4(0.0));
        return;
    }

    float density = computeFogDensity(worldPos);
    if (density <= 0.0) {
        imageStore(scatteringVolume, froxelCoord, vec4(0.0));
        return;
    }

    float sigmaS = scatterParams.x * density;
    float sigmaA = scatterParams.y * density;
    float sigmaT = sigmaS + sigmaA;
    float anisotropy = scatterParams.z;

    vec3 viewDir = normalize(worldPos - cameraPosition.xyz);
    vec3 inScattered = vec3(0.0);

    float ambientIntensity = ambientParams.x;
    inScattered += fogColor.rgb * fogColor.a * ambientIntensity;

    float viewZ = distFromCamera;

    for (uint i = 0; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];
        vec3 L = -normalize(light.direction);

        float phase = henyeyGreenstein(dot(viewDir, L), anisotropy);
        vec3 lightContrib = light.color * light.intensity * phase;

        float shadowFactor = 1.0;
        if (light.shadowIndex >= 0) {
            shadowFactor = sampleDirectionalShadowVolumetric(light.shadowIndex, worldPos, viewZ);
            // Mix with shadow intensity to allow partial shadow strength
            shadowFactor = mix(1.0, shadowFactor, 1.0 - lightCounts.shadowIntensity);
        }

        inScattered += lightContrib * shadowFactor;
    }

    uint clusterIndex = froxelToClusterIndex(froxelCoord, dims);
    ClusterLightData clusterData = clusterLightGrid[clusterIndex];

    uint pointCount = clusterData.counts & 0xFFFFu;
    uint spotCount = clusterData.counts >> 16u;

    for (uint i = 0; i < pointCount; ++i) {
        uint lightIndex = lightIndexList[clusterData.offset + i] & LIGHT_INDEX_MASK;
        PointLight light = pointLights[lightIndex];

        vec3 toLight = light.position - worldPos;
        float dist = length(toLight);
        if (dist > light.radius) continue;

        vec3 L = toLight / dist;
        float attenuation = physicalAttenuation(dist, light.radius);
        float phase = henyeyGreenstein(dot(viewDir, L), anisotropy);

        inScattered += light.color * light.intensity * attenuation * phase;
    }

    for (uint i = 0; i < spotCount; ++i) {
        uint packedIndex = lightIndexList[clusterData.offset + pointCount + i];
        uint lightIndex = packedIndex & LIGHT_INDEX_MASK;
        SpotLight light = spotLights[lightIndex];

        vec3 toLight = light.position - worldPos;
        float dist = length(toLight);
        if (dist > light.range) continue;

        vec3 L = toLight / dist;
        float distAtt = physicalAttenuation(dist, light.range);
        float spotAtt = spotAngleAttenuation(L, light.direction, light.cosInnerAngle, light.cosOuterAngle);
        if (spotAtt <= 0.0) continue;

        float phase = henyeyGreenstein(dot(viewDir, L), anisotropy);

        inScattered += light.color * light.intensity * distAtt * spotAtt * phase;
    }

    // Store: rgb = in-scattered light * scattering coefficient, a = extinction coefficient
    imageStore(scatteringVolume, froxelCoord, vec4(inScattered * sigmaS, sigmaT));
}
