#type COMPUTE
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// ============================================================
// Constants
// ============================================================
const uint MAX_LIGHTS_PER_CLUSTER = 64;
const uint PHASE_RESET = 0;
const uint PHASE_POINT_LIGHTS = 1;
const uint PHASE_SPOT_LIGHTS = 2;

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

// Must match GPUClusterGridParams in ClusterGridTypes.hpp (144 bytes)
struct ClusterGridParams {
    uvec4 gridDimensions;   // xyz = tilesX, tilesY, slicesZ, w = totalClusters
    vec4 screenParams;      // xy = screenSize, zw = tileSizePixels
    vec4 depthParams;       // x = near, y = far, z = log(far/near), w = 1/log(far/near)
    mat4 invProjection;     // 64 bytes
    vec4 clusterScale;      // xyz = scale factors
    vec4 clusterBias;       // xyz = bias factors
};

layout(std140, set = 0, binding = 0) uniform ClusterParamsUBO {
    ClusterGridParams clusterParams;
};

// Must match GPUClusterAABB in ClusterGridTypes.hpp (32 bytes)
struct ClusterAABB {
    vec4 minPoint;  // xyz = min corner (view-space)
    vec4 maxPoint;  // xyz = max corner (view-space)
};

layout(std430, set = 0, binding = 1) readonly buffer ClusterAABBBuffer {
    ClusterAABB clusterAABBs[];
};

// ============================================================
// Light Data (Set 1 - from GPULightBufferManager)
// Bindings must match GPULightBufferManager::createDescriptorSetLayout()
// ============================================================

// Must match GPUDirectionalLight in GPULightTypes.hpp (32 bytes)
// Not used in light culling, but binding must exist for layout compatibility
struct DirectionalLight {
    vec3 direction;
    float intensity;
    vec3 color;
    uint padding;
};

layout(std430, set = 1, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight directionalLights[];
};

// Must match GPUPointLight in GPULightTypes.hpp (32 bytes)
struct PointLight {
    vec3 position;      // World space
    float radius;
    vec3 color;
    float intensity;
};

layout(std430, set = 1, binding = 1) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

// Must match GPUSpotLight in GPULightTypes.hpp (64 bytes)
struct SpotLight {
    vec3 position;      // World space
    float range;
    vec3 direction;     // World space
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

// Must match GPULightCounts in GPULightTypes.hpp (16 bytes)
// Not used directly - counts passed via push constants
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
// Light Culling Output (Set 2 - our output buffers)
// ============================================================

// Must match GPUClusterLightData in LightCullingTypes.hpp (8 bytes)
struct ClusterLightData {
    uint offset;    // Offset into lightIndexList
    uint counts;    // lower 16 bits = point count, upper 16 bits = spot count
};

layout(std430, set = 2, binding = 0) buffer ClusterLightGridBuffer {
    ClusterLightData clusterLightGrid[];
};

// Light index list - flat array storing light indices for all clusters
// Point light indices are stored directly
// Spot light indices have bit 31 set to distinguish them
layout(std430, set = 2, binding = 1) buffer ClusterLightIndexListBuffer {
    uint lightIndexList[];
};

// Must match LightCullingGlobals in LightCullingTypes.hpp (16 bytes)
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
// Intersection Tests
// ============================================================

// Sphere-AABB intersection test (for point lights)
// Returns true if sphere intersects or is inside AABB
bool sphereIntersectsAABB(vec3 center, float radius, vec3 aabbMin, vec3 aabbMax) {
    // Find the closest point on AABB to sphere center
    vec3 closestPoint = clamp(center, aabbMin, aabbMax);

    // Check if distance from center to closest point is less than radius
    vec3 diff = center - closestPoint;
    float distSq = dot(diff, diff);
    return distSq <= (radius * radius);
}

// Cone-AABB intersection using bounding sphere approximation (for spot lights)
// This is a conservative test - may report some false positives but no false negatives
bool coneIntersectsAABB(vec3 apex, vec3 direction, float range, float cosOuterAngle,
                        vec3 aabbMin, vec3 aabbMax) {
    // Compute sin from cos
    float sinAngle = sqrt(max(0.0, 1.0 - cosOuterAngle * cosOuterAngle));

    // Compute cone's maximum radius at range distance
    float coneRadius = range * sinAngle;

    // Create a conservative bounding sphere that encompasses the cone
    // Sphere center is at apex + range/2 * direction
    // Sphere radius is the larger of half-range or max cone radius
    vec3 sphereCenter = apex + direction * (range * 0.5);
    float sphereRadius = max(range * 0.5, coneRadius);

    return sphereIntersectsAABB(sphereCenter, sphereRadius, aabbMin, aabbMax);
}

// ============================================================
// Helper Functions
// ============================================================

uint packLightCounts(uint pointCount, uint spotCount) {
    return (spotCount << 16) | (pointCount & 0xFFFF);
}

void unpackLightCounts(uint packed, out uint pointCount, out uint spotCount) {
    pointCount = packed & 0xFFFF;
    spotCount = packed >> 16;
}

// ============================================================
// Main Entry Point
// ============================================================
void main() {
    uint idx = gl_GlobalInvocationID.x;

    // ========================================
    // PHASE 0: Reset
    // ========================================
    if (pc.phase == PHASE_RESET) {
        // Reset cluster light data
        if (idx < pc.totalClusters) {
            clusterLightGrid[idx].offset = 0;
            clusterLightGrid[idx].counts = 0;
        }

        // Reset global counters (only thread 0)
        if (idx == 0) {
            globals.globalLightIndexCounter = 0;
            globals.overflowFlag = 0;
            globals.totalPointLightsAssigned = 0;
            globals.totalSpotLightsAssigned = 0;
        }
        return;
    }

    // ========================================
    // PHASE 1: Point Light Culling
    // ========================================
    if (pc.phase == PHASE_POINT_LIGHTS) {
        if (idx >= pc.pointLightCount) return;

        PointLight light = pointLights[idx];

        // Transform light position to view space
        vec3 viewPos = (pc.viewMatrix * vec4(light.position, 1.0)).xyz;
        float radius = light.radius;

        // Test against all clusters
        for (uint clusterIdx = 0; clusterIdx < pc.totalClusters; ++clusterIdx) {
            vec3 aabbMin = clusterAABBs[clusterIdx].minPoint.xyz;
            vec3 aabbMax = clusterAABBs[clusterIdx].maxPoint.xyz;

            if (sphereIntersectsAABB(viewPos, radius, aabbMin, aabbMax)) {
                // Get current counts
                uint currentCounts = atomicAdd(clusterLightGrid[clusterIdx].counts, 0);
                uint pointCount, spotCount;
                unpackLightCounts(currentCounts, pointCount, spotCount);

                // Check per-cluster limit
                if (pointCount < MAX_LIGHTS_PER_CLUSTER) {
                    // Allocate space in global light index list
                    uint globalOffset = atomicAdd(globals.globalLightIndexCounter, 1);

                    // Check for global overflow
                    if (globalOffset < lightIndexList.length()) {
                        // Store light index
                        lightIndexList[globalOffset] = idx;

                        // Update cluster's point light count (increment lower 16 bits)
                        atomicAdd(clusterLightGrid[clusterIdx].counts, 1);
                        atomicAdd(globals.totalPointLightsAssigned, 1);
                    } else {
                        // Global overflow
                        atomicOr(globals.overflowFlag, 1);
                    }
                }
            }
        }
        return;
    }

    // ========================================
    // PHASE 2: Spot Light Culling
    // ========================================
    if (pc.phase == PHASE_SPOT_LIGHTS) {
        if (idx >= pc.spotLightCount) return;

        SpotLight light = spotLights[idx];

        // Transform light position and direction to view space
        vec3 viewPos = (pc.viewMatrix * vec4(light.position, 1.0)).xyz;
        vec3 viewDir = normalize((pc.viewMatrix * vec4(light.direction, 0.0)).xyz);

        // Test against all clusters
        for (uint clusterIdx = 0; clusterIdx < pc.totalClusters; ++clusterIdx) {
            vec3 aabbMin = clusterAABBs[clusterIdx].minPoint.xyz;
            vec3 aabbMax = clusterAABBs[clusterIdx].maxPoint.xyz;

            if (coneIntersectsAABB(viewPos, viewDir, light.range, light.cosOuterAngle,
                                    aabbMin, aabbMax)) {
                // Get current counts
                uint currentCounts = atomicAdd(clusterLightGrid[clusterIdx].counts, 0);
                uint pointCount, spotCount;
                unpackLightCounts(currentCounts, pointCount, spotCount);

                uint totalInCluster = pointCount + spotCount;

                // Check per-cluster limit
                if (totalInCluster < MAX_LIGHTS_PER_CLUSTER) {
                    // Allocate space in global light index list
                    uint globalOffset = atomicAdd(globals.globalLightIndexCounter, 1);

                    // Check for global overflow
                    if (globalOffset < lightIndexList.length()) {
                        // Store spot light index with high bit set to distinguish from point lights
                        lightIndexList[globalOffset] = idx | 0x80000000u;

                        // Update cluster's spot light count (increment upper 16 bits)
                        atomicAdd(clusterLightGrid[clusterIdx].counts, 0x10000);
                        atomicAdd(globals.totalSpotLightsAssigned, 1);
                    } else {
                        // Global overflow
                        atomicOr(globals.overflowFlag, 1);
                    }
                }
            }
        }
        return;
    }
}
