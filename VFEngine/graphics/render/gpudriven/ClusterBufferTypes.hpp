#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>

// Include base cluster types (GPUCluster, GPUClusterSelection, toGPUCluster)
#include "resource/ClusterDAGTypes.hpp"

namespace render::gpudriven
{
    // =========================================================================
    // Constants for Cluster Buffers
    // =========================================================================

    // Note: MAX_GPU_CLUSTERS is defined in resource/ClusterDAGTypes.hpp (included above)

    // Maximum clusters that can be selected per frame
    constexpr uint32_t MAX_CLUSTER_SELECTIONS_PER_FRAME = 1024 * 1024;

    // Maximum cluster DAGs (per-submesh DAGs)
    constexpr uint32_t MAX_CLUSTER_DAGS = 16384;

    // Maximum streaming units
    constexpr uint32_t MAX_STREAMING_UNITS = 65536;

    // Maximum work queue entries (for DAG traversal)
    constexpr uint32_t MAX_WORK_QUEUE_ENTRIES = 512 * 1024;

    // Traversal modes
    constexpr uint32_t CLUSTER_TRAVERSE_TOP_DOWN = 0;
    constexpr uint32_t CLUSTER_TRAVERSE_BOTTOM_UP = 1;

    // Cluster flags for GPU (mirror resource::ClusterFlags)
    constexpr uint16_t GPU_CLUSTER_FLAG_IS_LEAF = 1 << 0;
    constexpr uint16_t GPU_CLUSTER_FLAG_IS_ROOT = 1 << 1;
    constexpr uint16_t GPU_CLUSTER_FLAG_HAS_LEFT_CHILD = 1 << 2;
    constexpr uint16_t GPU_CLUSTER_FLAG_HAS_RIGHT_CHILD = 1 << 3;
    constexpr uint16_t GPU_CLUSTER_FLAG_IS_BOUNDARY = 1 << 4;

    // Invalid cluster index sentinel
    constexpr uint32_t INVALID_GPU_CLUSTER_INDEX = 0xFFFFFFFF;

    // =========================================================================
    // GPUClusterDAGHeader - Per-mesh DAG metadata for GPU (64 bytes)
    // =========================================================================

    struct alignas(16) GPUClusterDAGHeader
    {
        // Counts
        uint32_t clusterCount;       // +0  Total clusters in this DAG
        uint32_t leafClusterCount;   // +4  Number of leaf clusters
        uint32_t maxDepth;           // +8  Maximum DAG depth
        uint32_t clusterOffset;      // +12 Offset into global cluster buffer

        // Error bounds
        float maxGeometricError;     // +16 Maximum error (at root)
        float minGeometricError;     // +20 Minimum error (at leaves, usually 0)
        uint32_t rootClusterIndex;   // +24 Index of root cluster
        uint32_t streamingUnitCount; // +28 Number of streaming units

        // Bounding volume
        glm::vec4 boundingSphere;    // +32 Overall bounds

        // Reserved for future use
        uint32_t reserved[4];        // +48
    };

    static_assert(sizeof(GPUClusterDAGHeader) == 64, "GPUClusterDAGHeader must be 64 bytes");
    static_assert(alignof(GPUClusterDAGHeader) == 16, "GPUClusterDAGHeader must be 16-byte aligned");

    // =========================================================================
    // GPUClusterTraversalParams - Per-frame traversal parameters (64 bytes)
    // =========================================================================

    struct alignas(16) GPUClusterTraversalParams
    {
        // Camera/projection for error calculation
        float projectionFactor;      // +0  screenHeight / (2 * tan(fovY/2))
        float screenErrorThreshold;  // +4  Pixel error threshold
        float errorMultiplier;       // +8  Global error scaling
        uint32_t maxClustersToSelect;// +12 Budget limit

        // Traversal control
        uint32_t traversalMode;      // +16 0=top-down, 1=bottom-up
        uint32_t enableCulling;      // +20 Enable frustum/backface cull
        uint32_t enableOcclusion;    // +24 Enable HiZ occlusion
        uint32_t frameIndex;         // +28 For temporal coherence

        // Cluster budget
        uint32_t targetTriangleCount;// +32 Target triangle budget
        uint32_t maxTriangleCount;   // +36 Hard triangle limit
        uint32_t currentSelectedCount;// +40 Atomic counter output
        uint32_t maxWorkQueueEntries;// +44 Work queue capacity for bounds checking

        // Reserved for future use
        glm::vec4 reserved;          // +48
    };

    static_assert(sizeof(GPUClusterTraversalParams) == 64, "GPUClusterTraversalParams must be 64 bytes");
    static_assert(alignof(GPUClusterTraversalParams) == 16, "GPUClusterTraversalParams must be 16-byte aligned");

    // =========================================================================
    // GPUClusterStreamingUnit - Streaming chunk descriptor (32 bytes)
    // =========================================================================

    struct alignas(16) GPUClusterStreamingUnit
    {
        // Cluster range in this unit
        uint32_t clusterStartIndex;  // +0  First cluster in this unit
        uint32_t clusterCount;       // +4  Number of clusters
        uint32_t meshletStartOffset; // +8  First meshlet offset
        uint32_t meshletCount;       // +12 Total meshlets in unit

        // Error range for this unit
        float minGeometricError;     // +16 Min error in this unit
        float maxGeometricError;     // +20 Max error in this unit
        uint32_t state;              // +24 Streaming state
        uint32_t priority;           // +28 Streaming priority (lower = higher)
    };

    static_assert(sizeof(GPUClusterStreamingUnit) == 32, "GPUClusterStreamingUnit must be 32 bytes");
    static_assert(alignof(GPUClusterStreamingUnit) == 16, "GPUClusterStreamingUnit must be 16-byte aligned");

    // =========================================================================
    // GPUClusterChildren - Child indices for DAG traversal (8 bytes)
    // =========================================================================

    struct GPUClusterChildren
    {
        uint32_t leftChild;   // +0 Index of left child (INVALID_GPU_CLUSTER_INDEX if none)
        uint32_t rightChild;  // +4 Index of right child (INVALID_GPU_CLUSTER_INDEX if none)
    };

    static_assert(sizeof(GPUClusterChildren) == 8, "GPUClusterChildren must be 8 bytes");

    // =========================================================================
    // GPUDAGTraversalState - Per-frame traversal state for multi-pass (32 bytes)
    // =========================================================================

    struct alignas(16) GPUDAGTraversalState
    {
        uint32_t inputQueueCount;      // +0  Number of items to process this pass
        uint32_t outputQueueCount;     // +4  Number of items queued for next pass
        uint32_t selectedCount;        // +8  Total clusters selected (atomic)
        uint32_t passIndex;            // +12 Current traversal pass number

        uint32_t totalProcessed;         // +16 Statistics: total clusters processed
        uint32_t totalSelected;          // +20 Statistics: total clusters selected
        uint32_t totalSubtreesCulled;    // +24 Statistics: subtrees pruned by culling
        uint32_t workQueueOverflowCount; // +28 Statistics: work items dropped due to queue overflow
    };

    static_assert(sizeof(GPUDAGTraversalState) == 32, "GPUDAGTraversalState must be 32 bytes");
    static_assert(alignof(GPUDAGTraversalState) == 16, "GPUDAGTraversalState must be 16-byte aligned");

} // namespace render::gpudriven
