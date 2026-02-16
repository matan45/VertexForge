#type COMPUTE
#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Volumetric Light Injection Compute Shader
// Injects light contributions into the froxel scattering volume
// Each invocation processes one froxel cell

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
struct ShadowData {
    mat4 viewProjection;
    vec4 atlasViewport;
    vec4 biasParams;    // x = depthBias, y = slopeBias, z = normalBias, w = texelSize
    vec4 rangeParams;   // x = near, y = far, z = cascadeCount, w = cascadeIndex
    vec4 pcfParams;     // x = kernelRadius, y = softness, z = filterEnabled, w = cubeMapIndex
};

layout(std430, set = 4, binding = 0) readonly buffer ShadowDataBuffer {
    ShadowData shadowData[];
};

// Set 5: Shadow Textures
layout(set = 5, binding = 0) uniform sampler2DShadow shadowAtlas;
layout(set = 5, binding = 1) uniform sampler2DArrayShadow shadowCascades;
layout(set = 5, binding = 2) uniform samplerCubeShadow shadowCubes[];

// Shadow constants (must match ShadowTypes.hpp)
const int MAX_SHADOW_VIEWS = 272;
const int MAX_POINT_SHADOW_CUBES = 32;

// Constants
const float VOL_PI = 3.14159265359;
const float LIGHT_INTENSITY_SCALE = 100.0;
const uint LIGHT_INDEX_MASK = 0x7FFFFFFFu;

// Henyey-Greenstein phase function
float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * VOL_PI * pow(denom, 1.5));
}

// Logarithmic depth slice to linear depth
float sliceToDepth(float slice, float near, float far, float numSlices) {
    float t = slice / numSlices;
    return near * pow(far / near, t);
}

// Reconstruct world-space position from froxel coordinates
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

// Compute fog density at world position
float computeFogDensity(vec3 worldPos) {
    float density = fogParams.x; // uniform density

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

// Map froxel to cluster index
uint froxelToClusterIndex(ivec3 froxelCoord, uvec3 volDims) {
    // Map froxel XY to screen-space tile, then compute cluster index
    float screenX = (float(froxelCoord.x) + 0.5) / float(volDims.x) * clusterParams.screenParams.x;
    float screenY = (float(froxelCoord.y) + 0.5) / float(volDims.y) * clusterParams.screenParams.y;

    uint tileX = uint(screenX / clusterParams.screenParams.z);
    uint tileY = uint(screenY / clusterParams.screenParams.w);

    // Compute depth for this froxel
    float near = depthParams.x;
    float far = depthParams.y;
    float depth = sliceToDepth(float(froxelCoord.z) + 0.5, near, far, float(volDims.z));

    // Map to cluster depth slice using cluster grid's log distribution
    float clusterNear = clusterParams.depthParams.x;
    float logRatio = log(max(depth, clusterNear) / clusterNear);
    uint slice = uint(logRatio * clusterParams.depthParams.w);

    tileX = min(tileX, clusterParams.gridDimensions.x - 1u);
    tileY = min(tileY, clusterParams.gridDimensions.y - 1u);
    slice = min(slice, clusterParams.gridDimensions.z - 1u);

    return tileX + tileY * clusterParams.gridDimensions.x +
           slice * clusterParams.gridDimensions.x * clusterParams.gridDimensions.y;
}

// Shadow sampling for cascade shadows (simplified for volumetric - no PCF needed)
float sampleCascadeShadowSimple(int shadowIndex, vec3 worldPos) {
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = shadowData[shadowIndex];

    vec4 lightSpacePos = sd.viewProjection * vec4(worldPos, 1.0);
    if (lightSpacePos.w <= 0.0) return 1.0;

    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;
    texCoords = clamp(texCoords, 0.0, 1.0);
    projCoords.xy = sd.atlasViewport.xy + texCoords * sd.atlasViewport.zw;
    projCoords.z = clamp(projCoords.z, 0.0, 1.0);

    // Single-tap shadow for volumetric (PCF not needed per-froxel)
    return texture(shadowAtlas, vec3(projCoords.xy, projCoords.z));
}

// Directional shadow with cascade selection
float sampleDirectionalShadowVolumetric(int baseShadowIndex, vec3 worldPos, float viewZ) {
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

// Point light attenuation
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

// Spot light angle attenuation
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

    // Reconstruct world-space position
    vec3 worldPos = froxelToWorld(froxelCoord, dims);

    // Check max distance
    float distFromCamera = length(worldPos - cameraPosition.xyz);
    if (distFromCamera > scatterParams.w) {
        imageStore(scatteringVolume, froxelCoord, vec4(0.0));
        return;
    }

    // Compute fog density
    float density = computeFogDensity(worldPos);
    if (density <= 0.0) {
        imageStore(scatteringVolume, froxelCoord, vec4(0.0));
        return;
    }

    float sigmaS = scatterParams.x * density; // scattering coefficient
    float sigmaA = scatterParams.y * density; // absorption coefficient
    float sigmaT = sigmaS + sigmaA;           // extinction coefficient
    float anisotropy = scatterParams.z;

    vec3 viewDir = normalize(cameraPosition.xyz - worldPos);
    vec3 inScattered = vec3(0.0);

    // Ambient contribution
    float ambientIntensity = ambientParams.x;
    inScattered += fogColor.rgb * fogColor.a * ambientIntensity;

    // Compute view-space Z for cascade selection
    float viewZ = distFromCamera;

    // Directional lights (with shadow sampling)
    for (uint i = 0; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];
        vec3 L = -normalize(light.direction);

        float phase = henyeyGreenstein(dot(viewDir, L), anisotropy);
        vec3 lightContrib = light.color * light.intensity * phase;

        // Shadow attenuation
        float shadowFactor = 1.0;
        if (light.shadowIndex >= 0) {
            shadowFactor = sampleDirectionalShadowVolumetric(light.shadowIndex, worldPos, viewZ);
            // Mix with shadow intensity to allow partial shadow strength
            shadowFactor = mix(1.0, shadowFactor, 1.0 - lightCounts.shadowIntensity);
        }

        inScattered += lightContrib * shadowFactor;
    }

    // Clustered point and spot lights
    uint clusterIndex = froxelToClusterIndex(froxelCoord, dims);
    ClusterLightData clusterData = clusterLightGrid[clusterIndex];

    uint pointCount = clusterData.counts & 0xFFFFu;
    uint spotCount = clusterData.counts >> 16u;

    // Point lights
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

    // Spot lights
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
