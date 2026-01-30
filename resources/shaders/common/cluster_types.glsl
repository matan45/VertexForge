#ifndef CLUSTER_TYPES_GLSL
#define CLUSTER_TYPES_GLSL

// =========================================================================
// Cluster Types for GPU
// Must match C++ structs in ClusterBufferTypes.hpp
// =========================================================================

// Must match GPUCluster in ClusterBufferTypes.hpp (64 bytes)
// GLSL packing: meshletCount/triangleCount and level/flags are packed into uint
struct GPUCluster {
    uint meshletOffset;           // +0
    uint vertexOffset;            // +4
    uint meshletTrianglePacked;   // +8  meshletCount | (triangleCount << 16)
    uint vertexCount;             // +12
    vec4 boundingSphere;          // +16
    vec4 cone;                    // +32
    uint parentIndex;             // +48
    uint siblingIndex;            // +52
    float geometricError;         // +56
    uint levelFlags;              // +60  level | (flags << 16)
};

// Must match GPUClusterSelection in ClusterBufferTypes.hpp (16 bytes)
struct GPUClusterSelection {
    uint clusterIndex;
    uint isSelected;
    float screenError;
    uint padding;
};

// Must match GPUClusterDAGHeader in ClusterBufferTypes.hpp (64 bytes)
struct GPUClusterDAGHeader {
    uint clusterCount;
    uint leafClusterCount;
    uint maxDepth;
    uint clusterOffset;
    float maxGeometricError;
    float minGeometricError;
    uint rootClusterIndex;
    uint streamingUnitCount;
    vec4 boundingSphere;
    uvec4 reserved;
};

// Must match GPUClusterTraversalParams in ClusterBufferTypes.hpp (64 bytes)
struct GPUClusterTraversalParams {
    float projectionFactor;
    float screenErrorThreshold;
    float errorMultiplier;
    uint maxClustersToSelect;
    uint traversalMode;
    uint enableCulling;
    uint enableOcclusion;
    uint frameIndex;
    uint targetTriangleCount;
    uint maxTriangleCount;
    uint currentSelectedCount;
    uint padding0;
    vec4 reserved;
};

// Must match GPUClusterStreamingUnit in ClusterBufferTypes.hpp (32 bytes)
struct GPUClusterStreamingUnit {
    uint clusterStartIndex;
    uint clusterCount;
    uint meshletStartOffset;
    uint meshletCount;
    float minGeometricError;
    float maxGeometricError;
    uint state;
    uint priority;
};

// =========================================================================
// Cluster Flags (must match GPU_CLUSTER_FLAG_* in ClusterBufferTypes.hpp)
// =========================================================================

const uint CLUSTER_FLAG_IS_LEAF = 1u << 0;
const uint CLUSTER_FLAG_IS_ROOT = 1u << 1;
const uint CLUSTER_FLAG_HAS_LEFT_CHILD = 1u << 2;
const uint CLUSTER_FLAG_HAS_RIGHT_CHILD = 1u << 3;
const uint CLUSTER_FLAG_IS_BOUNDARY = 1u << 4;

// Invalid cluster index sentinel
const uint INVALID_CLUSTER_INDEX = 0xFFFFFFFFu;

// Traversal modes
const uint CLUSTER_TRAVERSE_TOP_DOWN = 0u;
const uint CLUSTER_TRAVERSE_BOTTOM_UP = 1u;

// =========================================================================
// Helper Functions
// =========================================================================

// Unpack meshlet and triangle counts from packed uint
void unpackClusterCounts(uint packed, out uint meshletCount, out uint triangleCount) {
    meshletCount = packed & 0xFFFFu;
    triangleCount = (packed >> 16) & 0xFFFFu;
}

// Unpack level and flags from packed uint
void unpackClusterLevelFlags(uint packed, out uint level, out uint flags) {
    level = packed & 0xFFFFu;
    flags = (packed >> 16) & 0xFFFFu;
}

// Compute screen-space error from geometric error and distance
// projFactor = screenHeight / (2 * tan(fovY/2))
float computeClusterScreenError(float geometricError, float distance, float projFactor) {
    if (distance <= 0.0) return 1e10;
    return (geometricError / distance) * projFactor;
}

// Check if cluster should be selected based on error threshold
// Returns true if screen error (after multiplier) is below threshold
bool shouldSelectCluster(float screenError, float threshold, float multiplier) {
    return (screenError * multiplier) < threshold;
}

// Check if cluster is a leaf (no children)
bool isClusterLeaf(uint flags) {
    return (flags & CLUSTER_FLAG_IS_LEAF) != 0u;
}

// Check if cluster is the root (no parent)
bool isClusterRoot(uint flags) {
    return (flags & CLUSTER_FLAG_IS_ROOT) != 0u;
}

// Check if cluster has any children
bool clusterHasChildren(uint flags) {
    return (flags & (CLUSTER_FLAG_HAS_LEFT_CHILD | CLUSTER_FLAG_HAS_RIGHT_CHILD)) != 0u;
}

#endif // CLUSTER_TYPES_GLSL
