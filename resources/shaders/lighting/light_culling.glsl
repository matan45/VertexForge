#type COMPUTE
#version 450

// Cluster-centric light culling with shared memory tiling
// Each thread handles ONE cluster and tests against ALL lights
// Lights are loaded into shared memory in batches for cache efficiency

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// ============================================================
// Constants
// ============================================================
const uint MAX_LIGHTS_PER_CLUSTER = 64;
const uint LIGHTS_PER_BATCH = 64;  // Match workgroup size for optimal loading
const uint PHASE_RESET = 0;
const uint PHASE_CULL_LIGHTS = 1;  // Single phase for all light types

// Light index packing scheme:
// Point lights: index stored as-is (bits 0-30)
// Spot lights: high bit (bit 31) set to distinguish from point lights
// This allows both types to share the same index list
const uint SPOT_LIGHT_FLAG = 0x80000000u;

// ============================================================
// Push Constants
// ============================================================
layout(push_constant) uniform PushConstants {
    mat4 viewMatrix;
    uint pointLightCount;
    uint spotLightCount;
    uint totalClusters;
    uint phase;
} pc;

// ============================================================
// Cluster Grid Data (Set 0 - from ClusterGridManager)
// ============================================================

struct ClusterGridParams {
    uvec4 gridDimensions;
    vec4 screenParams;
    vec4 depthParams;
    mat4 invProjection;
    vec4 clusterScale;
    vec4 clusterBias;
};

layout(std140, set = 0, binding = 0) uniform ClusterParamsUBO {
    ClusterGridParams clusterParams;
};

struct ClusterAABB {
    vec4 minPoint;
    vec4 maxPoint;
};

layout(std430, set = 0, binding = 1) readonly buffer ClusterAABBBuffer {
    ClusterAABB clusterAABBs[];
};

// ============================================================
// Light Data (Set 1 - from GPULightBufferManager)
// ============================================================

struct DirectionalLight {
    vec3 direction;
    float intensity;
    vec3 color;
    uint padding;
};

layout(std430, set = 1, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight directionalLights[];
};

struct PointLight {
    vec3 position;
    float radius;
    vec3 color;
    float intensity;
};

layout(std430, set = 1, binding = 1) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

struct SpotLight {
    vec3 position;
    float range;
    vec3 direction;
    float intensity;
    vec3 color;
    float cosInnerAngle;
    float cosOuterAngle;
    float padding0;
    float padding1;
    float padding2;
};

layout(std430, set = 1, binding = 2) readonly buffer SpotLightBuffer {
    SpotLight spotLights[];
};

struct LightCounts {
    uint directionalCount;
    uint pointCount;
    uint spotCount;
    uint padding;
};

layout(std140, set = 1, binding = 3) uniform LightCountsUBO {
    LightCounts lightCounts;
};

// ============================================================
// Light Culling Output (Set 2)
// ============================================================

struct ClusterLightData {
    uint offset;
    uint counts;
};

layout(std430, set = 2, binding = 0) buffer ClusterLightGridBuffer {
    ClusterLightData clusterLightGrid[];
};

layout(std430, set = 2, binding = 1) buffer ClusterLightIndexListBuffer {
    uint lightIndexList[];
};

struct LightCullingGlobals {
    uint globalLightIndexCounter;
    uint overflowFlag;
    uint totalPointLightsAssigned;
    uint totalSpotLightsAssigned;
};

layout(std430, set = 2, binding = 2) buffer GlobalsBuffer {
    LightCullingGlobals globals;
};

// ============================================================
// Shared Memory for Light Tiling
// ============================================================

// Cached point light data in view space (reduced from 32 bytes to 16 bytes)
struct CachedPointLight {
    vec3 viewPos;
    float radius;
};

// Cached spot light data in view space (reduced from 64 bytes to 32 bytes)
struct CachedSpotLight {
    vec3 viewPos;
    float range;
    vec3 viewDir;
    float cosOuterAngle;
};

shared CachedPointLight sharedPointLights[LIGHTS_PER_BATCH];
shared CachedSpotLight sharedSpotLights[LIGHTS_PER_BATCH];

// ============================================================
// Intersection Tests
// ============================================================

bool sphereIntersectsAABB(vec3 center, float radius, vec3 aabbMin, vec3 aabbMax) {
    vec3 closestPoint = clamp(center, aabbMin, aabbMax);
    vec3 diff = center - closestPoint;
    return dot(diff, diff) <= (radius * radius);
}

bool coneIntersectsAABB(vec3 apex, vec3 direction, float range, float cosOuterAngle,
                        vec3 aabbMin, vec3 aabbMax) {
    float sinAngle = sqrt(max(0.0, 1.0 - cosOuterAngle * cosOuterAngle));
    float coneRadius = range * sinAngle;
    vec3 sphereCenter = apex + direction * (range * 0.5);
    float sphereRadius = max(range * 0.5, coneRadius);
    return sphereIntersectsAABB(sphereCenter, sphereRadius, aabbMin, aabbMax);
}

// ============================================================
// Main Entry Point
// ============================================================
void main() {
    uint localIdx = gl_LocalInvocationID.x;
    uint clusterIdx = gl_GlobalInvocationID.x;

    // ========================================
    // PHASE 0: Reset
    // ========================================
    if (pc.phase == PHASE_RESET) {
        if (clusterIdx < pc.totalClusters) {
            clusterLightGrid[clusterIdx].offset = clusterIdx * MAX_LIGHTS_PER_CLUSTER;
            clusterLightGrid[clusterIdx].counts = 0;
        }

        if (clusterIdx == 0) {
            globals.globalLightIndexCounter = 0;
            globals.overflowFlag = 0;
            globals.totalPointLightsAssigned = 0;
            globals.totalSpotLightsAssigned = 0;
        }
        return;
    }

    // ========================================
    // PHASE 1: Cluster-Centric Light Culling
    // ========================================
    if (pc.phase == PHASE_CULL_LIGHTS) {
        // Each thread handles one cluster
        bool validCluster = (clusterIdx < pc.totalClusters);

        // Load cluster AABB (only if valid)
        vec3 aabbMin, aabbMax;
        uint clusterOffset = 0;
        if (validCluster) {
            aabbMin = clusterAABBs[clusterIdx].minPoint.xyz;
            aabbMax = clusterAABBs[clusterIdx].maxPoint.xyz;
            clusterOffset = clusterLightGrid[clusterIdx].offset;
        }

        // Per-thread counters (no atomics needed - each thread owns its cluster)
        uint pointCount = 0;
        uint spotCount = 0;

        // ----------------------------------------
        // Process Point Lights in Batches
        // ----------------------------------------
        uint numPointBatches = (pc.pointLightCount + LIGHTS_PER_BATCH - 1) / LIGHTS_PER_BATCH;

        for (uint batch = 0; batch < numPointBatches; ++batch) {
            // Cooperative loading: each thread loads one light into shared memory
            uint lightToLoad = batch * LIGHTS_PER_BATCH + localIdx;
            if (lightToLoad < pc.pointLightCount) {
                PointLight light = pointLights[lightToLoad];
                sharedPointLights[localIdx].viewPos = (pc.viewMatrix * vec4(light.position, 1.0)).xyz;
                sharedPointLights[localIdx].radius = light.radius;
            }

            // Synchronize: ensure all lights are loaded before testing
            barrier();

            // Each thread tests its cluster against all lights in this batch
            if (validCluster) {
                uint batchEnd = min(LIGHTS_PER_BATCH, pc.pointLightCount - batch * LIGHTS_PER_BATCH);

                for (uint i = 0; i < batchEnd; ++i) {
                    if (pointCount + spotCount >= MAX_LIGHTS_PER_CLUSTER) break;

                    CachedPointLight cachedLight = sharedPointLights[i];

                    if (sphereIntersectsAABB(cachedLight.viewPos, cachedLight.radius, aabbMin, aabbMax)) {
                        uint globalLightIdx = batch * LIGHTS_PER_BATCH + i;
                        lightIndexList[clusterOffset + pointCount + spotCount] = globalLightIdx;
                        pointCount++;
                    }
                }
            }

            // Synchronize before loading next batch
            barrier();
        }

        // ----------------------------------------
        // Process Spot Lights in Batches
        // ----------------------------------------
        uint numSpotBatches = (pc.spotLightCount + LIGHTS_PER_BATCH - 1) / LIGHTS_PER_BATCH;

        for (uint batch = 0; batch < numSpotBatches; ++batch) {
            // Cooperative loading: each thread loads one light into shared memory
            uint lightToLoad = batch * LIGHTS_PER_BATCH + localIdx;
            if (lightToLoad < pc.spotLightCount) {
                SpotLight light = spotLights[lightToLoad];
                sharedSpotLights[localIdx].viewPos = (pc.viewMatrix * vec4(light.position, 1.0)).xyz;
                sharedSpotLights[localIdx].range = light.range;
                sharedSpotLights[localIdx].viewDir = normalize((pc.viewMatrix * vec4(light.direction, 0.0)).xyz);
                sharedSpotLights[localIdx].cosOuterAngle = light.cosOuterAngle;
            }

            // Synchronize: ensure all lights are loaded before testing
            barrier();

            // Each thread tests its cluster against all lights in this batch
            if (validCluster) {
                uint batchEnd = min(LIGHTS_PER_BATCH, pc.spotLightCount - batch * LIGHTS_PER_BATCH);

                for (uint i = 0; i < batchEnd; ++i) {
                    if (pointCount + spotCount >= MAX_LIGHTS_PER_CLUSTER) break;

                    CachedSpotLight cachedLight = sharedSpotLights[i];

                    if (coneIntersectsAABB(cachedLight.viewPos, cachedLight.viewDir,
                                           cachedLight.range, cachedLight.cosOuterAngle,
                                           aabbMin, aabbMax)) {
                        uint globalLightIdx = batch * LIGHTS_PER_BATCH + i;
                        lightIndexList[clusterOffset + pointCount + spotCount] = globalLightIdx | SPOT_LIGHT_FLAG;
                        spotCount++;
                    }
                }
            }

            // Synchronize before loading next batch
            barrier();
        }

        // ----------------------------------------
        // Write final counts (no atomics needed)
        // ----------------------------------------
        if (validCluster) {
            clusterLightGrid[clusterIdx].counts = (spotCount << 16) | pointCount;

            // Update global stats (atomics only for stats, not for correctness)
            if (pointCount > 0) atomicAdd(globals.totalPointLightsAssigned, pointCount);
            if (spotCount > 0) atomicAdd(globals.totalSpotLightsAssigned, spotCount);
        }

        return;
    }
}
