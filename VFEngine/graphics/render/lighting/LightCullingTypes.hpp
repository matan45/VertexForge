#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <cstddef>

namespace render::lighting
{
    // Light culling constants
    // Uses FIXED ALLOCATION strategy: each cluster gets MAX_LIGHTS_PER_CLUSTER slots
    // This eliminates the need for a prefix-sum pass to compute offsets
    // Memory budget for 3456 clusters (16x9x24):
    //   - ClusterLightGrid: 3456 clusters x 8 bytes = ~27KB
    //   - ClusterLightIndexList: 3456 clusters x 64 lights x 4 bytes = ~884KB
    //   - LightCullingGlobals: 16 bytes
    //   - Total: ~911KB
    namespace LightCullingConstants
    {
        // Max lights per cluster - balance between memory and scene complexity
        // 64 lights/cluster allows for complex lighting while keeping memory reasonable
        inline constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 64;

        // Workgroup size for light culling compute shader
        // 64 is optimal for most GPUs (matches wave/warp size)
        inline constexpr uint32_t LIGHT_CULL_WORKGROUP_SIZE = 64;

        // Phase constants for compute shader dispatch
        // Using cluster-centric approach: single culling phase for all light types
        inline constexpr uint32_t PHASE_RESET = 0;
        inline constexpr uint32_t PHASE_CULL_LIGHTS = 1;
    }

    // Compute light index list size based on cluster count (fixed allocation)
    // Each cluster gets MAX_LIGHTS_PER_CLUSTER slots
    inline constexpr uint32_t computeLightIndexListSize(uint32_t clusterCount)
    {
        return clusterCount * LightCullingConstants::MAX_LIGHTS_PER_CLUSTER;
    }

    // Per-cluster light assignment data
    // Stored in ClusterLightGrid buffer, one entry per cluster
    // Layout: [offset, counts] where counts = (spotCount << 16) | pointCount
    struct alignas(8) GPUClusterLightData
    {
        uint32_t offset;    // 4 bytes - Offset into ClusterLightIndexList
        uint32_t counts;    // 4 bytes - lower 16 bits = point count, upper 16 bits = spot count
    };
    static_assert(sizeof(GPUClusterLightData) == 8, "GPUClusterLightData must be 8 bytes");
    static_assert(offsetof(GPUClusterLightData, offset) == 0, "GPUClusterLightData::offset offset mismatch");
    static_assert(offsetof(GPUClusterLightData, counts) == 4, "GPUClusterLightData::counts offset mismatch");

    // Push constants for light culling compute shader
    // Contains per-dispatch parameters including view matrix and phase
    struct alignas(16) LightCullingPushConstants
    {
        glm::mat4 viewMatrix;       // 64 bytes - View matrix for transforming lights to view space
        uint32_t pointLightCount;   // 4 bytes - Number of point lights in scene
        uint32_t spotLightCount;    // 4 bytes - Number of spot lights in scene
        uint32_t totalClusters;     // 4 bytes - Total number of clusters in grid
        uint32_t phase;             // 4 bytes - Current dispatch phase (reset/point/spot)
    };
    static_assert(sizeof(LightCullingPushConstants) == 80, "LightCullingPushConstants must be 80 bytes");
    static_assert(offsetof(LightCullingPushConstants, viewMatrix) == 0, "LightCullingPushConstants::viewMatrix offset mismatch");
    static_assert(offsetof(LightCullingPushConstants, pointLightCount) == 64, "LightCullingPushConstants::pointLightCount offset mismatch");
    static_assert(offsetof(LightCullingPushConstants, spotLightCount) == 68, "LightCullingPushConstants::spotLightCount offset mismatch");
    static_assert(offsetof(LightCullingPushConstants, totalClusters) == 72, "LightCullingPushConstants::totalClusters offset mismatch");
    static_assert(offsetof(LightCullingPushConstants, phase) == 76, "LightCullingPushConstants::phase offset mismatch");

    // Global atomic counters and debug statistics for light culling
    // Used for atomic allocation of light indices and overflow detection
    struct alignas(16) LightCullingGlobals
    {
        uint32_t globalLightIndexCounter;   // 4 bytes - Atomic counter for index allocation
        uint32_t overflowFlag;              // 4 bytes - Set to 1 if any cluster overflowed
        uint32_t totalPointLightsAssigned;  // 4 bytes - Debug: total point light assignments
        uint32_t totalSpotLightsAssigned;   // 4 bytes - Debug: total spot light assignments
    };
    static_assert(sizeof(LightCullingGlobals) == 16, "LightCullingGlobals must be 16 bytes");
    static_assert(offsetof(LightCullingGlobals, globalLightIndexCounter) == 0, "LightCullingGlobals::globalLightIndexCounter offset mismatch");
    static_assert(offsetof(LightCullingGlobals, overflowFlag) == 4, "LightCullingGlobals::overflowFlag offset mismatch");
    static_assert(offsetof(LightCullingGlobals, totalPointLightsAssigned) == 8, "LightCullingGlobals::totalPointLightsAssigned offset mismatch");
    static_assert(offsetof(LightCullingGlobals, totalSpotLightsAssigned) == 12, "LightCullingGlobals::totalSpotLightsAssigned offset mismatch");

    // Helper functions for packing/unpacking light counts
    inline uint32_t packLightCounts(uint32_t pointCount, uint32_t spotCount)
    {
        return (spotCount << 16) | (pointCount & 0xFFFF);
    }

    inline void unpackLightCounts(uint32_t packed, uint32_t& pointCount, uint32_t& spotCount)
    {
        pointCount = packed & 0xFFFF;
        spotCount = packed >> 16;
    }
}
