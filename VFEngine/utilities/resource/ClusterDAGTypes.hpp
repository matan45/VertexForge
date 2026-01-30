#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <cmath>

namespace resource
{
    // =========================================================================
    // Constants
    // =========================================================================

    // Maximum clusters per submesh (supports ~2M triangle meshes with 128-tri clusters)
    constexpr uint32_t MAX_CLUSTERS_PER_SUBMESH = 16384;

    // Maximum DAG depth (log2 of MAX_CLUSTERS_PER_SUBMESH + margin)
    constexpr uint32_t MAX_DAG_DEPTH = 16;

    // Target triangles per cluster (matches Nanite's ~128 triangles)
    constexpr uint32_t TARGET_CLUSTER_TRIANGLES = 128;

    // Maximum meshlets per cluster (clusters group meshlets)
    constexpr uint32_t MAX_MESHLETS_PER_CLUSTER = 4;

    // Invalid index sentinel (matches BVH pattern)
    constexpr uint32_t INVALID_CLUSTER_INDEX = 0xFFFFFFFF;

    // Root cluster is always at index 0
    constexpr uint32_t ROOT_CLUSTER_INDEX = 0;

    // Default threshold for LOD selection (in pixels)
    // If screenError < threshold, can use this cluster instead of children
    constexpr float DEFAULT_SCREEN_ERROR_THRESHOLD = 1.0f;

    // Default error multiplier for tuning transition distances
    // Higher values = transitions happen farther from camera (more aggressive LOD)
    // Lower values = transitions happen closer (higher quality)
    constexpr float DEFAULT_ERROR_MULTIPLIER = 1.0f;

    // =========================================================================
    // ErrorConfig - Configurable error settings for LOD selection
    // =========================================================================

    struct ErrorConfig
    {
        // Screen-space error threshold in pixels
        float screenErrorThreshold = DEFAULT_SCREEN_ERROR_THRESHOLD;

        // Multiplier applied to computed screen error before threshold comparison
        float errorMultiplier = DEFAULT_ERROR_MULTIPLIER;

        // Apply multiplier to computed screen error
        [[nodiscard]] float adjustedScreenError(float rawScreenError) const
        {
            return rawScreenError * errorMultiplier;
        }

        // Check if cluster should be selected (error below threshold)
        // Returns true if this cluster's error is acceptable for rendering
        [[nodiscard]] bool shouldSelectCluster(float screenError) const
        {
            return adjustedScreenError(screenError) < screenErrorThreshold;
        }
    };

    // =========================================================================
    // ClusterFlags - Bit flags for cluster state
    // =========================================================================

    namespace ClusterFlags
    {
        constexpr uint16_t None          = 0;
        constexpr uint16_t IsLeaf        = 1 << 0;  // No children, finest detail
        constexpr uint16_t IsRoot        = 1 << 1;  // No parent, coarsest level
        constexpr uint16_t HasLeftChild  = 1 << 2;  // Has first child
        constexpr uint16_t HasRightChild = 1 << 3;  // Has second child
        constexpr uint16_t IsBoundary    = 1 << 4;  // At LOD transition boundary
    }

    // =========================================================================
    // ClusterDescriptor - Describes cluster's meshlet data location (16 bytes)
    // =========================================================================

    struct ClusterDescriptor
    {
        // Offset into the global meshlet array for this cluster's meshlets
        uint32_t meshletOffset;

        // Number of meshlets in this cluster (1-4 typically)
        uint16_t meshletCount;

        // Triangle count in this cluster (for stats/debugging)
        uint16_t triangleCount;

        // Offset into global vertex buffer (base vertex for this cluster)
        uint32_t vertexOffset;

        // Number of unique vertices in this cluster
        uint32_t vertexCount;
    };

    static_assert(sizeof(ClusterDescriptor) == 16, "ClusterDescriptor must be 16 bytes");

    // =========================================================================
    // ClusterBounds - Spatial bounds for culling (32 bytes)
    // Matches MeshletBounds pattern for consistency
    // =========================================================================

    struct ClusterBounds
    {
        // Bounding sphere: xyz = center (local space), w = radius
        glm::vec4 boundingSphere;

        // Normal cone for backface culling: xyz = axis, w = cos(half-angle)
        // If cone.w >= 1.0, backface culling is disabled for this cluster
        glm::vec4 cone;
    };

    static_assert(sizeof(ClusterBounds) == 32, "ClusterBounds must be 32 bytes");

    // =========================================================================
    // ClusterHierarchy - DAG relationship and LOD error (16 bytes)
    // =========================================================================

    struct ClusterHierarchy
    {
        // Parent cluster index (INVALID_CLUSTER_INDEX for root)
        uint32_t parentIndex;

        // Sibling cluster index (the other child of our parent)
        // Used for coarsening: when both siblings can be replaced by parent
        uint32_t siblingIndex;

        // Geometric error: maximum deviation introduced when this cluster
        // replaces its children. Measured in local/object space units.
        // Leaf clusters have error = 0.0
        float geometricError;

        // DAG level: 0 = root (coarsest), increases toward leaves
        // Leaves have level = maxDepth
        uint16_t level;

        // Flags for cluster state (see ClusterFlags namespace)
        uint16_t flags;
    };

    static_assert(sizeof(ClusterHierarchy) == 16, "ClusterHierarchy must be 16 bytes");

    // =========================================================================
    // Cluster - Complete cluster node for CPU storage (64 bytes)
    // =========================================================================

    struct Cluster
    {
        ClusterDescriptor descriptor;  // 16 bytes
        ClusterBounds bounds;          // 32 bytes
        ClusterHierarchy hierarchy;    // 16 bytes

        [[nodiscard]] bool isLeaf() const
        {
            return (hierarchy.flags & ClusterFlags::IsLeaf) != 0;
        }

        [[nodiscard]] bool isRoot() const
        {
            return (hierarchy.flags & ClusterFlags::IsRoot) != 0;
        }

        [[nodiscard]] bool hasParent() const
        {
            return hierarchy.parentIndex != INVALID_CLUSTER_INDEX;
        }

        [[nodiscard]] bool hasSibling() const
        {
            return hierarchy.siblingIndex != INVALID_CLUSTER_INDEX;
        }

        [[nodiscard]] bool hasLeftChild() const
        {
            return (hierarchy.flags & ClusterFlags::HasLeftChild) != 0;
        }

        [[nodiscard]] bool hasRightChild() const
        {
            return (hierarchy.flags & ClusterFlags::HasRightChild) != 0;
        }

        [[nodiscard]] bool hasChildren() const
        {
            return hasLeftChild() || hasRightChild();
        }
    };

    static_assert(sizeof(Cluster) == 64, "Cluster must be 64 bytes");

    // =========================================================================
    // ClusterDAGHeader - Per-submesh DAG metadata (64 bytes)
    // =========================================================================

    struct ClusterDAGHeader
    {
        // Total number of clusters in this DAG
        uint32_t clusterCount;

        // Number of leaf clusters (finest detail)
        uint32_t leafClusterCount;

        // Maximum depth of the DAG (0 = single cluster)
        uint32_t maxDepth;

        // Index of first cluster in global cluster array
        uint32_t clusterOffset;

        // Maximum geometric error in the DAG (root's error)
        float maxGeometricError;

        // Padding for alignment
        float padding0;

        // Bounding sphere of entire submesh (matches root cluster bounds)
        glm::vec4 boundingSphere;

        // Reserved for future use (streaming state, etc.)
        uint32_t reserved[6];
    };

    static_assert(sizeof(ClusterDAGHeader) == 64, "ClusterDAGHeader must be 64 bytes");

    // =========================================================================
    // ClusterDAGData - Complete DAG for a submesh (CPU storage)
    // =========================================================================

    struct ClusterDAGData
    {
        ClusterDAGHeader header{};
        std::vector<Cluster> clusters;

        // Helper: Get cluster at index (bounds-checked)
        [[nodiscard]] const Cluster* getCluster(uint32_t index) const
        {
            if (index >= clusters.size()) return nullptr;
            return &clusters[index];
        }

        // Helper: Get mutable cluster at index (bounds-checked)
        [[nodiscard]] Cluster* getCluster(uint32_t index)
        {
            if (index >= clusters.size()) return nullptr;
            return &clusters[index];
        }

        // Helper: Get parent of cluster
        [[nodiscard]] const Cluster* getParent(const Cluster& cluster) const
        {
            if (!cluster.hasParent()) return nullptr;
            return getCluster(cluster.hierarchy.parentIndex);
        }

        // Helper: Get sibling of cluster
        [[nodiscard]] const Cluster* getSibling(const Cluster& cluster) const
        {
            if (!cluster.hasSibling()) return nullptr;
            return getCluster(cluster.hierarchy.siblingIndex);
        }

        // Helper: Get children indices (returns count: 0, 1, or 2)
        [[nodiscard]] uint32_t getChildrenIndices(uint32_t clusterIndex,
                                                   uint32_t outChildren[2]) const
        {
            uint32_t count = 0;
            for (uint32_t i = 0; i < static_cast<uint32_t>(clusters.size()); ++i)
            {
                if (clusters[i].hierarchy.parentIndex == clusterIndex)
                {
                    outChildren[count++] = i;
                    if (count == 2) break;
                }
            }
            return count;
        }

        // Helper: Get all leaf clusters
        [[nodiscard]] std::vector<uint32_t> getLeafIndices() const
        {
            std::vector<uint32_t> leaves;
            leaves.reserve(header.leafClusterCount);
            for (uint32_t i = 0; i < static_cast<uint32_t>(clusters.size()); ++i)
            {
                if (clusters[i].isLeaf())
                {
                    leaves.push_back(i);
                }
            }
            return leaves;
        }

        // Validate DAG integrity
        [[nodiscard]] bool isValid() const
        {
            if (clusters.empty()) return header.clusterCount == 0;
            if (clusters.size() != header.clusterCount) return false;

            // Root must be at index 0 and have no parent
            if (!clusters[0].isRoot()) return false;
            if (clusters[0].hasParent()) return false;

            // Verify leaf count
            uint32_t leafCount = 0;
            for (const auto& cluster : clusters)
            {
                if (cluster.isLeaf()) ++leafCount;
            }
            if (leafCount != header.leafClusterCount) return false;

            return true;
        }

        // Clear all data
        void clear()
        {
            header = ClusterDAGHeader{};
            clusters.clear();
        }
    };

    // =========================================================================
    // ClusterCut - Represents selected clusters for rendering
    // A "cut" through the DAG where all triangles are covered exactly once
    // =========================================================================

    struct ClusterCut
    {
        // Indices of selected clusters (forms a valid cut through DAG)
        std::vector<uint32_t> selectedClusters;

        // Total triangle count in the cut
        uint32_t triangleCount = 0;

        // Total meshlet count in the cut
        uint32_t meshletCount = 0;

        void clear()
        {
            selectedClusters.clear();
            triangleCount = 0;
            meshletCount = 0;
        }
    };

    // =========================================================================
    // Screen-Space Error Calculation Helpers
    // =========================================================================

    // Calculate screen-space error from geometric error
    // geometricError: object-space maximum deviation
    // distanceToCamera: world-space distance from camera to cluster center
    // screenHeight: viewport height in pixels
    // fovY: vertical field of view in radians
    inline float calculateScreenSpaceError(
        float geometricError,
        float distanceToCamera,
        float screenHeight,
        float fovY)
    {
        if (distanceToCamera <= 0.0f) return 1e10f; // Very close, use finest LOD

        // Project error to screen space using perspective projection
        // screenError = (geometricError / distance) * (screenHeight / (2 * tan(fovY/2)))
        float projectionFactor = screenHeight / (2.0f * std::tan(fovY * 0.5f));
        return (geometricError / distanceToCamera) * projectionFactor;
    }

    // Compute projection factor once per frame for efficiency
    inline float computeProjectionFactor(float screenHeight, float fovY)
    {
        return screenHeight / (2.0f * std::tan(fovY * 0.5f));
    }

    // Fast screen-space error with precomputed projection factor
    inline float calculateScreenSpaceErrorFast(
        float geometricError,
        float distanceToCamera,
        float projectionFactor)
    {
        if (distanceToCamera <= 0.0f) return 1e10f;
        return (geometricError / distanceToCamera) * projectionFactor;
    }

    // =========================================================================
    // ClusterLODSelector - Runtime helper for cluster LOD selection
    // =========================================================================

    class ClusterLODSelector
    {
    public:
        ClusterLODSelector() = default;

        // Initialize with camera/screen parameters (call once per frame)
        void beginFrame(float screenHeight, float fovY, const ErrorConfig& config = {})
        {
            projectionFactor_ = computeProjectionFactor(screenHeight, fovY);
            config_ = config;
        }

        // Compute raw screen error for a cluster (without multiplier)
        [[nodiscard]] float computeScreenError(
            float geometricError,
            float distanceToCamera) const
        {
            return calculateScreenSpaceErrorFast(
                geometricError, distanceToCamera, projectionFactor_);
        }

        // Determine if cluster should be rendered (vs drilling down to children)
        // Returns true if this cluster's error is acceptable
        [[nodiscard]] bool shouldRenderCluster(
            float geometricError,
            float distanceToCamera) const
        {
            float screenError = computeScreenError(geometricError, distanceToCamera);
            return config_.shouldSelectCluster(screenError);
        }

        // Get the adjusted screen error for a cluster (with multiplier applied)
        [[nodiscard]] float getAdjustedScreenError(
            float geometricError,
            float distanceToCamera) const
        {
            float screenError = computeScreenError(geometricError, distanceToCamera);
            return config_.adjustedScreenError(screenError);
        }

        // Get current projection factor (for external use)
        [[nodiscard]] float getProjectionFactor() const { return projectionFactor_; }

        // Get current error config
        [[nodiscard]] const ErrorConfig& getConfig() const { return config_; }

    private:
        float projectionFactor_ = 0.0f;
        ErrorConfig config_;
    };

} // namespace resource

// =========================================================================
// GPU Structures - For GPU buffer access
// =========================================================================

namespace render::gpudriven
{
    // Maximum clusters in GPU buffer
    constexpr uint32_t MAX_GPU_CLUSTERS = 4 * 1024 * 1024;

    // =========================================================================
    // GPUCluster - GPU-optimized cluster layout (64 bytes, 16-byte aligned)
    // =========================================================================

    struct alignas(16) GPUCluster
    {
        // Descriptor data (16 bytes)
        uint32_t meshletOffset;      // +0
        uint32_t vertexOffset;       // +4
        uint16_t meshletCount;       // +8
        uint16_t triangleCount;      // +10
        uint32_t vertexCount;        // +12

        // Bounding sphere (16 bytes)
        glm::vec4 boundingSphere;    // +16

        // Normal cone (16 bytes)
        glm::vec4 cone;              // +32

        // Hierarchy data (16 bytes)
        uint32_t parentIndex;        // +48
        uint32_t siblingIndex;       // +52
        float geometricError;        // +56
        uint16_t level;              // +60
        uint16_t flags;              // +62
    };

    static_assert(sizeof(GPUCluster) == 64, "GPUCluster must be 64 bytes");
    static_assert(alignof(GPUCluster) == 16, "GPUCluster must be 16-byte aligned");
    static_assert(offsetof(GPUCluster, boundingSphere) == 16, "boundingSphere offset must be 16");
    static_assert(offsetof(GPUCluster, cone) == 32, "cone offset must be 32");
    static_assert(offsetof(GPUCluster, parentIndex) == 48, "parentIndex offset must be 48");

    // =========================================================================
    // GPUClusterSelection - Per-frame cluster selection state (16 bytes)
    // =========================================================================

    struct alignas(16) GPUClusterSelection
    {
        // Cluster index in the global cluster array
        uint32_t clusterIndex;

        // Selected for rendering this frame (1) or not (0)
        uint32_t isSelected;

        // Projected screen-space error (pixels)
        float screenError;

        // Padding for alignment
        uint32_t padding;
    };

    static_assert(sizeof(GPUClusterSelection) == 16, "GPUClusterSelection must be 16 bytes");
    static_assert(alignof(GPUClusterSelection) == 16, "GPUClusterSelection must be 16-byte aligned");

    // =========================================================================
    // Conversion helper: CPU Cluster to GPU Cluster
    // =========================================================================

    inline GPUCluster toGPUCluster(const resource::Cluster& cluster)
    {
        GPUCluster gpu{};
        gpu.meshletOffset = cluster.descriptor.meshletOffset;
        gpu.vertexOffset = cluster.descriptor.vertexOffset;
        gpu.meshletCount = cluster.descriptor.meshletCount;
        gpu.triangleCount = cluster.descriptor.triangleCount;
        gpu.vertexCount = cluster.descriptor.vertexCount;
        gpu.boundingSphere = cluster.bounds.boundingSphere;
        gpu.cone = cluster.bounds.cone;
        gpu.parentIndex = cluster.hierarchy.parentIndex;
        gpu.siblingIndex = cluster.hierarchy.siblingIndex;
        gpu.geometricError = cluster.hierarchy.geometricError;
        gpu.level = cluster.hierarchy.level;
        gpu.flags = cluster.hierarchy.flags;
        return gpu;
    }

} // namespace render::gpudriven
