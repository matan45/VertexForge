#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <queue>
#include <future>
#include <mutex>
#include <unordered_map>
#include <string>
#include <memory>
#include <cfloat>

namespace core
{
    class Device;
}

namespace resource
{
    struct ClusterDAGData;
    class MeshStreamHandle;
}

namespace render::gpudriven
{
    class ClusterBuffer;
    class MeshletBuffer;
    enum class ClusterStreamState : uint8_t;
}

namespace render::mesh
{
    // =========================================================================
    // ClusterStreamingRequest - Priority queue element for streaming
    // =========================================================================

    struct ClusterStreamingRequest
    {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex = 0;
        float priority = 0.0f;           // Lower = higher priority
        float screenError = 0.0f;        // From DAG traversal
        glm::vec3 worldCenter{0.0f};
        float boundingRadius = 0.0f;
        uint64_t lastVisibleFrame = 0;

        // Comparison for priority queue (min-heap: lower priority value = higher priority)
        bool operator<(const ClusterStreamingRequest& other) const
        {
            return priority > other.priority;
        }
    };

    // =========================================================================
    // ClusterStreamingResult - Result from async disk read
    // =========================================================================

    struct ClusterStreamingResult
    {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex = 0;
        std::unique_ptr<resource::ClusterDAGData> dagData;
        bool success = false;
    };

    // =========================================================================
    // ClusterStreamingState - Per-submesh streaming state
    // =========================================================================

    struct ClusterSubmeshState
    {
        gpudriven::ClusterStreamState state;
        uint64_t lastVisibleFrame = 0;
        float lastScreenError = FLT_MAX;
        size_t gpuMemoryUsed = 0;
        bool inQueue = false;  // Prevent duplicate queue entries
    };

    // =========================================================================
    // ClusterMeshState - Per-mesh tracking
    // =========================================================================

    struct ClusterMeshState
    {
        std::unique_ptr<resource::MeshStreamHandle> handle;
        uint32_t referenceCount = 0;
        bool headerParsed = false;
        std::unordered_map<std::string, ClusterSubmeshState> submeshStates;  // key: submeshName#submeshIdx
    };

    // =========================================================================
    // PendingUpload - Queued for GPU upload on main thread
    // =========================================================================

    struct ClusterPendingUpload
    {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex = 0;
        std::unique_ptr<resource::ClusterDAGData> dagData;
        uint32_t baseMeshletOffset = 0;
    };

    // =========================================================================
    // ClusterStreamManager - Priority-based cluster streaming system (VK-294)
    // =========================================================================

    class ClusterStreamManager
    {
    private:
        core::Device& device;
        gpudriven::ClusterBuffer& clusterBuffer;
        gpudriven::MeshletBuffer* meshletBuffer = nullptr;

        // Per-mesh file handles and states
        std::unordered_map<std::string, ClusterMeshState> meshStates;
        mutable std::mutex meshStatesMutex;

        // Priority queue for streaming requests
        std::priority_queue<ClusterStreamingRequest> streamingQueue;
        mutable std::mutex queueMutex;

        // Async I/O tracking
        std::vector<std::future<ClusterStreamingResult>> pendingReads;
        mutable std::mutex pendingReadsMutex;

        // Pending GPU uploads (processed on main thread)
        std::vector<ClusterPendingUpload> pendingUploads;
        mutable std::mutex uploadMutex;

        // Memory budget management
        size_t maxGPUMemoryBudget = 512 * 1024 * 1024;   // 512 MB default
        size_t currentGPUMemoryUsed = 0;
        size_t maxBytesPerFrame = 8 * 1024 * 1024;       // 8 MB per frame
        size_t bytesStreamedThisFrame = 0;

        // Concurrency limits
        uint32_t maxPendingReads = 4;
        uint32_t maxPendingUploads = 8;

        // Frame tracking for LRU
        uint64_t currentFrame = 0;

    public:
        explicit ClusterStreamManager(core::Device& device, gpudriven::ClusterBuffer& clusterBuffer);
        ~ClusterStreamManager();

        // Non-copyable
        ClusterStreamManager(const ClusterStreamManager&) = delete;
        ClusterStreamManager& operator=(const ClusterStreamManager&) = delete;

        // =========================================================================
        // Lifecycle - Request/release mesh streaming
        // =========================================================================

        // Request cluster data for a mesh (called when mesh enters scene)
        void requestMesh(const std::string& meshPath);

        // Release mesh reference (called when mesh leaves scene)
        void releaseMesh(const std::string& meshPath);

        // =========================================================================
        // Frame Update - Call once per frame
        // =========================================================================

        void update(const glm::vec3& cameraPos, uint64_t frameIndex);

        // =========================================================================
        // Priority Requests - From DAG traversal feedback
        // =========================================================================

        // Request streaming with priority based on screen error
        void requestStreamingUnit(const std::string& meshPath,
                                  const std::string& submeshName,
                                  uint32_t submeshIndex,
                                  float screenError,
                                  const glm::vec3& worldCenter,
                                  float boundingRadius);

        // Mark cluster DAG as visible this frame (for LRU tracking)
        void markVisible(const std::string& meshPath,
                        const std::string& submeshName,
                        uint32_t submeshIndex,
                        float screenError);

        // =========================================================================
        // Query State
        // =========================================================================

        // Check if cluster DAG is fully loaded and ready for rendering
        bool isDAGReady(const std::string& meshPath,
                       const std::string& submeshName,
                       uint32_t submeshIndex) const;

        // Check if mesh has any cluster data (header parsed)
        bool hasClusterData(const std::string& meshPath) const;

        // =========================================================================
        // Configuration
        // =========================================================================

        void setMeshletBuffer(gpudriven::MeshletBuffer* buffer) { meshletBuffer = buffer; }
        void setMaxGPUMemoryBudget(size_t bytes) { maxGPUMemoryBudget = bytes; }
        void setMaxBytesPerFrame(size_t bytes) { maxBytesPerFrame = bytes; }
        void setMaxPendingReads(uint32_t count) { maxPendingReads = count; }
        void setMaxPendingUploads(uint32_t count) { maxPendingUploads = count; }

        // =========================================================================
        // Statistics
        // =========================================================================

        size_t getCurrentGPUMemoryUsed() const { return currentGPUMemoryUsed; }
        size_t getMaxGPUMemoryBudget() const { return maxGPUMemoryBudget; }
        float getMemoryUsagePercent() const
        {
            return maxGPUMemoryBudget > 0
                ? (static_cast<float>(currentGPUMemoryUsed) / static_cast<float>(maxGPUMemoryBudget)) * 100.0f
                : 0.0f;
        }
        uint32_t getPendingReadCount() const;
        uint32_t getQueuedRequestCount() const;
        uint32_t getPendingUploadCount() const;

    private:
        // =========================================================================
        // Internal Methods
        // =========================================================================

        // Mesh stream management
        void openMeshStream(const std::string& meshPath);
        void scheduleInitialClusters(const std::string& meshPath);

        // Streaming pipeline
        void processStreamingQueue();
        void processPendingReads();
        void processPendingUploads();

        // Priority management
        void updatePriorities(const glm::vec3& cameraPos);
        float calculatePriority(const ClusterStreamingRequest& request,
                               const glm::vec3& cameraPos) const;

        // Eviction
        void evictLRUClustersIfNeeded(size_t requiredBytes);
        std::vector<std::pair<std::string, std::string>> getLRUCandidates() const;

        // Async read
        std::future<ClusterStreamingResult> asyncReadClusterDAG(
            const std::string& meshPath,
            const std::string& submeshName,
            uint32_t submeshIndex);

        // Upload handling
        void handleCompletedRead(ClusterStreamingResult&& result);

        // Key generation
        static std::string makeSubmeshKey(const std::string& submeshName, uint32_t submeshIndex)
        {
            return submeshName + "#" + std::to_string(submeshIndex);
        }

        // Parse submesh key back to components
        static bool parseSubmeshKey(const std::string& key, std::string& outSubmeshName, uint32_t& outSubmeshIndex);
    };

} // namespace render::mesh
