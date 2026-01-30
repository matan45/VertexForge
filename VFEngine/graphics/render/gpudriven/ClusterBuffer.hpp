#pragma once

#include "ClusterBufferTypes.hpp"
#include "FreeListAllocator.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace core
{
    class Device;
    class TransferManager;
}

namespace resource
{
    struct MeshStreamHeader;
    struct ClusterDAGData;
    struct ClusterDAGFileInfo;
}

namespace render::gpudriven
{
    // =========================================================================
    // ClusterStreamState - Streaming state for cluster data
    // =========================================================================

    enum class ClusterStreamState : uint8_t
    {
        NotRequested,   // Not yet requested for streaming
        Queued,         // In streaming queue
        Streaming,      // Being read from disk
        Uploading,      // Being uploaded to GPU
        Ready,          // Available for rendering
        Evicted         // Was loaded but evicted from GPU
    };

    // =========================================================================
    // ClusterDAGAllocation - Per-submesh cluster DAG allocation tracking
    // =========================================================================

    struct ClusterDAGAllocation
    {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex = 0;

        // Cluster buffer allocation
        uint32_t clusterOffset = 0;      // Offset into global cluster buffer
        uint32_t clusterCount = 0;       // Total clusters in this DAG

        // DAG header buffer allocation
        uint32_t dagHeaderIndex = 0;     // Index into DAG header buffer

        // State tracking
        ClusterStreamState streamState = ClusterStreamState::NotRequested;
        bool isAllocated = false;

        // Metadata from file (cached for quick access)
        uint32_t leafClusterCount = 0;
        uint32_t maxDepth = 0;
        float maxGeometricError = 0.0f;
        glm::vec4 boundingSphere{0.0f};

        bool isReady() const { return streamState == ClusterStreamState::Ready; }
    };

    // =========================================================================
    // ClusterDAGInfo - Info struct for querying cluster data
    // =========================================================================

    struct ClusterDAGInfo
    {
        uint32_t clusterOffset = 0;      // Offset into global cluster buffer
        uint32_t clusterCount = 0;       // Total clusters
        uint32_t dagHeaderIndex = 0;     // Index into DAG header buffer
        uint32_t leafClusterCount = 0;
        float maxGeometricError = 0.0f;
    };

    // =========================================================================
    // ClusterBuffer - GPU buffer manager for cluster DAG data
    // =========================================================================

    class ClusterBuffer
    {
    private:
        core::Device& device;
        std::unique_ptr<core::TransferManager> transferManager;

        // GPU Buffers
        vk::Buffer clusterBuffer;                    // GPUCluster[] - cluster descriptors + bounds
        vk::DeviceMemory clusterBufferMemory;

        vk::Buffer dagHeaderBuffer;                  // GPUClusterDAGHeader[] - per-submesh metadata
        vk::DeviceMemory dagHeaderBufferMemory;

        vk::Buffer clusterSelectionBuffer;           // GPUClusterSelection[] - per-frame selection
        vk::DeviceMemory clusterSelectionBufferMemory;

        vk::Buffer streamingUnitBuffer;              // GPUClusterStreamingUnit[] - streaming chunks
        vk::DeviceMemory streamingUnitBufferMemory;

        // DAG Traversal buffers (VK-291)
        vk::Buffer clusterChildBuffer;               // GPUClusterChildren[] - child indices for traversal
        vk::DeviceMemory clusterChildBufferMemory;

        vk::Buffer traversalStateBuffer;             // GPUDAGTraversalState - per-frame traversal state
        vk::DeviceMemory traversalStateBufferMemory;

        vk::Buffer workQueueBufferA;                 // uint[] - work queue A (ping-pong)
        vk::DeviceMemory workQueueBufferAMemory;

        vk::Buffer workQueueBufferB;                 // uint[] - work queue B (ping-pong)
        vk::DeviceMemory workQueueBufferBMemory;

        // Capacity limits
        uint32_t maxClusterCount = 0;
        uint32_t maxDAGHeaderCount = 0;
        uint32_t maxSelectionCount = 0;
        uint32_t maxStreamingUnitCount = 0;
        uint32_t maxWorkQueueCount = 0;

        // Current usage
        uint32_t currentClusterCount = 0;
        uint32_t currentDAGHeaderCount = 0;
        uint32_t currentStreamingUnitCount = 0;

        // Allocators
        FreeListAllocator clusterAllocator;
        FreeListAllocator dagHeaderAllocator;
        FreeListAllocator streamingUnitAllocator;

        // Allocation tracking
        std::vector<ClusterDAGAllocation> allocations;
        std::unordered_map<std::string, size_t> allocationKeyToIndex;
        std::vector<size_t> freeAllocationSlots;

        bool initialized = false;

    public:
        explicit ClusterBuffer(core::Device& device);
        ~ClusterBuffer();

        ClusterBuffer(const ClusterBuffer&) = delete;
        ClusterBuffer& operator=(const ClusterBuffer&) = delete;

        // =========================================================================
        // Initialization
        // =========================================================================

        void init(uint32_t maxClusters = MAX_GPU_CLUSTERS,
                  uint32_t maxDAGHeaders = MAX_CLUSTER_DAGS,
                  uint32_t maxSelections = MAX_CLUSTER_SELECTIONS_PER_FRAME,
                  uint32_t maxStreamingUnits = MAX_STREAMING_UNITS,
                  uint32_t maxWorkQueueEntries = MAX_WORK_QUEUE_ENTRIES);

        void cleanup();

        // =========================================================================
        // Reservation - Allocate space for entire mesh cluster DAGs
        // =========================================================================

        ClusterDAGAllocation* reserveClusters(const std::string& meshPath,
                                              const resource::MeshStreamHeader& header);

        // =========================================================================
        // Upload - Transfer cluster data to GPU
        // =========================================================================

        bool uploadClusterDAG(const std::string& meshPath,
                              const std::string& submeshName,
                              uint32_t submeshIndex,
                              const resource::ClusterDAGData& dagData,
                              uint32_t baseMeshletOffset);

        // =========================================================================
        // Eviction - Free GPU memory
        // =========================================================================

        bool evictClusterDAG(const std::string& meshPath,
                             const std::string& submeshName,
                             uint32_t submeshIndex);

        void evictMesh(const std::string& meshPath);

        // =========================================================================
        // Query
        // =========================================================================

        const ClusterDAGAllocation* getAllocation(const std::string& meshPath,
                                                  const std::string& submeshName,
                                                  uint32_t submeshIndex) const;

        ClusterDAGInfo getClusterDAGInfo(const ClusterDAGAllocation& alloc) const;

        bool hasClusterData(const std::string& meshPath) const;

        // =========================================================================
        // State Management
        // =========================================================================

        void setStreamState(const std::string& meshPath,
                            const std::string& submeshName,
                            uint32_t submeshIndex,
                            ClusterStreamState state);

        ClusterStreamState getStreamState(const std::string& meshPath,
                                          const std::string& submeshName,
                                          uint32_t submeshIndex) const;

        // =========================================================================
        // Buffer Access
        // =========================================================================

        vk::Buffer getClusterBuffer() const { return clusterBuffer; }
        vk::Buffer getDAGHeaderBuffer() const { return dagHeaderBuffer; }
        vk::Buffer getClusterSelectionBuffer() const { return clusterSelectionBuffer; }
        vk::Buffer getStreamingUnitBuffer() const { return streamingUnitBuffer; }
        vk::Buffer getClusterChildBuffer() const { return clusterChildBuffer; }
        vk::Buffer getTraversalStateBuffer() const { return traversalStateBuffer; }
        vk::Buffer getWorkQueueBufferA() const { return workQueueBufferA; }
        vk::Buffer getWorkQueueBufferB() const { return workQueueBufferB; }

        // =========================================================================
        // Size Queries
        // =========================================================================

        size_t getClusterBufferSize() const { return maxClusterCount * sizeof(GPUCluster); }
        size_t getDAGHeaderBufferSize() const { return maxDAGHeaderCount * sizeof(GPUClusterDAGHeader); }
        size_t getClusterSelectionBufferSize() const { return maxSelectionCount * sizeof(GPUClusterSelection); }
        size_t getStreamingUnitBufferSize() const { return maxStreamingUnitCount * sizeof(GPUClusterStreamingUnit); }
        size_t getClusterChildBufferSize() const { return maxClusterCount * sizeof(GPUClusterChildren); }
        size_t getTraversalStateBufferSize() const { return sizeof(GPUDAGTraversalState); }
        size_t getWorkQueueBufferSize() const { return maxWorkQueueCount * sizeof(uint32_t); }

        size_t getTotalBufferSize() const
        {
            return getClusterBufferSize() + getDAGHeaderBufferSize() +
                   getClusterSelectionBufferSize() + getStreamingUnitBufferSize() +
                   getClusterChildBufferSize() + getTraversalStateBufferSize() +
                   getWorkQueueBufferSize() * 2; // Two work queue buffers
        }

        uint32_t getCurrentClusterCount() const { return currentClusterCount; }
        uint32_t getCurrentDAGHeaderCount() const { return currentDAGHeaderCount; }

        // =========================================================================
        // Transfer Management
        // =========================================================================

        void flushPendingTransfers();

        // Reset traversal state for new frame (clears all counters)
        void resetTraversalState();

    private:
        void createBuffers();
        void destroyBuffers();

        // Upload helpers
        void uploadClustersAt(uint32_t offset, const GPUCluster* data, uint32_t count);
        void uploadDAGHeaderAt(uint32_t index, const GPUClusterDAGHeader& header);
        void uploadStreamingUnitsAt(uint32_t offset, const GPUClusterStreamingUnit* data, uint32_t count);
        void uploadClusterChildrenAt(uint32_t offset, const GPUClusterChildren* data, uint32_t count);

        // Child index building helper
        void buildChildIndices(const std::vector<GPUCluster>& clusters,
                               std::vector<GPUClusterChildren>& outChildren);

        // Allocation helpers
        bool allocateClusterSpace(ClusterDAGAllocation& alloc,
                                  const resource::ClusterDAGFileInfo& dagInfo,
                                  const std::string& debugKey);

        void freeClusterSpace(ClusterDAGAllocation& alloc);

        // Key generation (matches MeshletBuffer pattern)
        static std::string makeAllocationKey(const std::string& meshPath,
                                             const std::string& submeshName,
                                             uint32_t submeshIndex)
        {
            return meshPath + ":" + submeshName + "#" + std::to_string(submeshIndex);
        }
    };

} // namespace render::gpudriven
