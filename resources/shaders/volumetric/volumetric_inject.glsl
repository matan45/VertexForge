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
    vec4 noiseParams;            // x = scale, y = intensity, z = timeOffset, w = octaves
};

layout(rgba16f, set = 0, binding = 1) uniform writeonly image3D scatteringVolume;
layout(set = 0, binding = 5) uniform sampler3D fogNoiseTexture;

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
    int shadowMode;   // 0 = cascade, 1 = clipmap
    uint _pad[3];
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
    uint rtShadowActive;
    uint _lcpad1;
    uint _lcpad2;
    uint _lcpad3;
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

// Set 6: Fog Volumes
struct GPUFogVolume {
    mat4 worldToLocal;
    vec4 boundsMin;
    vec4 boundsMax;
    vec4 albedoAndDensity;   // rgb = albedo, a = density
    vec4 emissionAndFalloff; // rgb = emission, a = edge falloff
    uint shapeType;          // 0=Box, 1=Sphere, 2=Cylinder
    uint blendMode;          // 0=Additive, 1=Subtractive
    int densityTextureIndex;
    uint _fvPad;
};

layout(std430, set = 6, binding = 0) readonly buffer FogVolumeBuffer {
    uint fogVolumeCount;
    uint _fvPad0;
    uint _fvPad1;
    uint _fvPad2;
    GPUFogVolume fogVolumes[];
};

float evaluateFogVolume(GPUFogVolume vol, vec3 worldPos) {
    // Quick AABB rejection
    if (any(lessThan(worldPos, vol.boundsMin.xyz)) || any(greaterThan(worldPos, vol.boundsMax.xyz)))
        return 0.0;

    // Transform to local unit space
    vec3 localPos = (vol.worldToLocal * vec4(worldPos, 1.0)).xyz;
    float dist;

    if (vol.shapeType == 0u) { // Box
        vec3 d = abs(localPos);
        dist = max(d.x, max(d.y, d.z));
    } else if (vol.shapeType == 1u) { // Sphere
        dist = length(localPos);
    } else { // Cylinder
        float radialDist = length(localPos.xz);
        dist = max(radialDist, abs(localPos.y));
    }

    if (dist > 1.0) return 0.0;

    // Edge falloff
    float falloff = vol.emissionAndFalloff.a;
    float weight = 1.0 - smoothstep(1.0 - falloff, 1.0, dist);

    return vol.albedoAndDensity.a * weight;
}

// Set 7: GI Probe Data (compute-compatible layout for ambient injection)
struct GIProbeData {
    vec4 shR0; vec4 shR1; vec4 shR2;
    vec4 shG0; vec4 shG1; vec4 shG2;
    vec4 shB0; vec4 shB1; vec4 shB2;
    vec4 validity;
};

struct GICascadeInfo {
    vec4 gridOriginSpacing;
    ivec4 gridDimsOffset;
};

layout(std430, set = 7, binding = 0) readonly buffer GIProbeBuffer {
    GIProbeData giProbes[];
};

layout(std430, set = 7, binding = 1) readonly buffer GICascadeBuffer {
    uint giCascadeCount;
    uint _giPad0; uint _giPad1; uint _giPad2;
    GICascadeInfo giCascades[];
};

const float GI_SH_C0 = 0.282095;
const float GI_SH_C1 = 0.488603;
const float GI_SH_C2 = 1.092548;
const float GI_SH_C20 = 0.315392;
const float GI_SH_C22 = 0.546274;

vec3 evaluateGISH(GIProbeData probe, vec3 n) {
    vec4 b0 = vec4(GI_SH_C0, GI_SH_C1 * n.y, GI_SH_C1 * n.z, GI_SH_C1 * n.x);
    vec4 b1 = vec4(GI_SH_C2 * n.x * n.y, GI_SH_C2 * n.y * n.z,
                   GI_SH_C20 * (3.0 * n.z * n.z - 1.0), GI_SH_C2 * n.x * n.z);
    float b2 = GI_SH_C22 * (n.x * n.x - n.y * n.y);
    return max(vec3(
        dot(probe.shR0, b0) + dot(probe.shR1, b1) + probe.shR2.x * b2,
        dot(probe.shG0, b0) + dot(probe.shG1, b1) + probe.shG2.x * b2,
        dot(probe.shB0, b0) + dot(probe.shB1, b1) + probe.shB2.x * b2
    ), vec3(0.0));
}

vec3 sampleFogGI(vec3 worldPos, float cameraDist) {
    if (giCascadeCount == 0u) return vec3(0.0);

    uint selectedCascade = 0u;
    for (uint i = 1u; i < giCascadeCount; ++i) {
        float range = giCascades[i].gridOriginSpacing.w * float(giCascades[i].gridDimsOffset.x) * 0.5;
        if (cameraDist > range * 0.7) selectedCascade = i;
    }

    GICascadeInfo cascade = giCascades[selectedCascade];
    float spacing = cascade.gridOriginSpacing.w;
    vec3 origin = cascade.gridOriginSpacing.xyz;
    ivec3 gridDims = cascade.gridDimsOffset.xyz;
    int probeOffset = cascade.gridDimsOffset.w;

    vec3 localPos = (worldPos - origin) / spacing;
    ivec3 baseCoord = clamp(ivec3(floor(localPos)), ivec3(0), gridDims - ivec3(2));
    vec3 alpha = fract(localPos);

    vec3 irradiance = vec3(0.0);
    float totalWeight = 0.0;

    for (int dz = 0; dz <= 1; ++dz) {
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dx = 0; dx <= 1; ++dx) {
                ivec3 coord = baseCoord + ivec3(dx, dy, dz);
                uint idx = uint(probeOffset) + uint(coord.x + coord.y * gridDims.x + coord.z * gridDims.x * gridDims.y);
                GIProbeData probe = giProbes[idx];
                vec3 w = mix(vec3(1.0) - alpha, alpha, vec3(dx, dy, dz));
                float weight = w.x * w.y * w.z * probe.validity.x;
                if (weight > 0.0) {
                    // Average SH over up direction for omnidirectional fog scattering
                    irradiance += evaluateGISH(probe, vec3(0, 1, 0)) * weight;
                    totalWeight += weight;
                }
            }
        }
    }

    return totalWeight > 0.0 ? clamp(irradiance / totalWeight, vec3(0.0), vec3(5.0)) : vec3(0.0);
}

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

// Halton low-discrepancy sequence for sub-voxel jittering
float halton(uint index, uint base) {
    float result = 0.0;
    float f = 1.0 / float(base);
    uint i = index;
    while (i > 0u) {
        result += f * float(i % base);
        i /= base;
        f /= float(base);
    }
    return result;
}

float sliceToDepth(float slice, float near, float far, float numSlices) {
    float t = slice / numSlices;
    return near * pow(far / near, t);
}

vec3 froxelToWorld(ivec3 froxelCoord, uvec3 dims, vec3 jitter) {
    vec2 uv = (vec2(froxelCoord.xy) + 0.5 + jitter.xy) / vec2(dims.xy);
    float near = depthParams.x;
    float far = depthParams.y;
    float depth = sliceToDepth(float(froxelCoord.z) + 0.5 + jitter.z, near, far, float(dims.z));

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

    // 3D noise modulation
    if (noiseParams.y > 0.0) {
        vec3 uvw = worldPos * noiseParams.x;
        uvw += vec3(noiseParams.z * 0.6, noiseParams.z * 0.2, noiseParams.z * 0.4);
        float noise = texture(fogNoiseTexture, uvw).r;
        float noiseModulation = 1.0 + (noise - 0.5) * 2.0 * noiseParams.y;
        density *= max(noiseModulation, 0.0);
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

    // Clamp inward by 1 texel to prevent seams at tile boundaries
    float borderTexel = 1.0 / PAGE_SIZE_F;
    pageUV = clamp(pageUV, vec2(borderTexel), vec2(1.0 - borderTexel));

    vec2 physicalUV = (vec2(float(tileX), float(tileY)) + pageUV) * (PAGE_SIZE_F / POOL_DIM_F);
    return physicalUV;
}

// Directional lights use RT shadows — no VSM sampling needed in volumetrics
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

    // Sub-voxel jitter using Halton(2,3,5) sequence for temporal super-sampling
    uint jitterIdx = pc.frameIndex % 16u;
    vec3 jitter = vec3(
        halton(jitterIdx + 1u, 2u) - 0.5,
        halton(jitterIdx + 1u, 3u) - 0.5,
        halton(jitterIdx + 1u, 5u) - 0.5
    );

    vec3 worldPos = froxelToWorld(froxelCoord, dims, jitter);
    float distFromCamera = length(worldPos - cameraPosition.xyz);
    if (distFromCamera > scatterParams.w) {
        imageStore(scatteringVolume, froxelCoord, vec4(0.0));
        return;
    }

    float density = computeFogDensity(worldPos);

    // Inject fog volume contributions
    vec3 fogVolumeEmission = vec3(0.0);
    vec3 fogVolumeAlbedo = vec3(0.0);
    float fogVolumeDensityTotal = 0.0;

    for (uint v = 0u; v < fogVolumeCount && v < 64u; ++v) {
        GPUFogVolume vol = fogVolumes[v];
        float volDensity = evaluateFogVolume(vol, worldPos);
        if (abs(volDensity) < 0.001) continue;

        if (vol.blendMode == 1u || volDensity < 0.0) { // Subtractive blend or negative density
            density -= abs(volDensity);
        } else { // Additive
            density += volDensity;
            fogVolumeAlbedo += vol.albedoAndDensity.rgb * volDensity;
            fogVolumeEmission += vol.emissionAndFalloff.rgb * volDensity;
            fogVolumeDensityTotal += volDensity;
        }
    }
    density = max(density, 0.0);

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

    // Blend fog color with fog volume albedo
    vec3 effectiveFogColor = fogColor.rgb;
    if (fogVolumeDensityTotal > 0.0) {
        float volumeWeight = fogVolumeDensityTotal / density;
        effectiveFogColor = mix(fogColor.rgb, fogVolumeAlbedo / fogVolumeDensityTotal, volumeWeight);
    }

    float ambientIntensity = ambientParams.x;
    float giIntensity = ambientParams.w;

    // GI probe injection: spatially-varying bounced light
    if (giIntensity > 0.0 && giCascadeCount > 0u) {
        vec3 giIrradiance = sampleFogGI(worldPos, distFromCamera);
        inScattered += giIrradiance * giIntensity;
        inScattered += effectiveFogColor * fogColor.a * ambientIntensity * 0.2;
    } else {
        inScattered += effectiveFogColor * fogColor.a * ambientIntensity;
    }

    // Add fog volume emission
    inScattered += fogVolumeEmission;

    float viewZ = distFromCamera;

    for (uint i = 0; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];
        vec3 L = -normalize(light.direction);

        float phase = henyeyGreenstein(dot(viewDir, L), anisotropy);
        vec3 lightContrib = light.color * light.intensity * phase;

        float shadowFactor = 1.0; // Directional lights use RT shadows, not VSM

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
