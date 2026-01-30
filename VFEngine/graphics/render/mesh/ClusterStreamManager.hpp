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
    class MergedMeshBuffer;
    enum class ClusterStreamState : uint8_t;
}

namespace render::mesh
{
    // =========================================================================
    // EvictionConfig - Configurable eviction parameters (VK-296)
    // =========================================================================

    struct EvictionConfig
    {
        // Recency threshold in frames (default ~1 second at 60fps)
        uint32_t recencyProtectionFrames = 60;

        // Weight factors for priority calculation (should sum to ~1.0)
        float recencyWeight = 0.5f;        // How much recency matters (higher = more important)
        float screenErrorWeight = 0.3f;    // Higher error = less important to keep
        float memorySizeWeight = 0.2f;     // Larger = more valuable to evict

        // Eviction behavior
        float minEvictionPercent = 0.10f;  // Minimum % of budget to free when evicting
        float targetEvictionPercent = 0.15f; // Target % to free for headroom

        // Streaming unit grouping
        bool enableGroupEviction = true;   // Evict entire streaming units together
        bool respectDependencies = true;   // Never evict parent before children
    };

    // =========================================================================
    // StreamingUnitState - Per-streaming-unit eviction tracking (VK-296)
    // =========================================================================

    struct StreamingUnitState
    {
        uint32_t unitIndex = 0;
        uint64_t lastVisibleFrame = 0;
        float lastScreenError = FLT_MAX;
        size_t gpuMemoryUsed = 0;
        uint32_t dependsOnUnit = 0xFFFFFFFF;  // Parent unit (INVALID if root)
        bool isLoaded = false;
    };

    // =========================================================================
    // EvictionCandidate - For sorting eviction candidates (VK-296)
    // =========================================================================

    struct EvictionCandidate
    {
        float priority = 0.0f;             // Lower = evict first
        size_t memorySize = 0;
        std::string meshPath;
        std::string submeshKey;
        uint32_t unitIndex = 0xFFFFFFFF;   // Specific unit or INVALID for whole DAG

        bool operator<(const EvictionCandidate& other) const
        {
            return priority < other.priority;  // Sort ascending: lowest priority evicted first
        }
    };

    // =========================================================================
    // EvictionStats - Statistics for debugging/monitoring (VK-296)
    // =========================================================================

    struct EvictionStats
    {
        uint64_t totalEvictions = 0;
        uint64_t groupEvictions = 0;
        uint64_t dependencyBlocks = 0;     // Times eviction blocked by dependency
        size_t totalBytesEvicted = 0;
        float averageEvictionPriority = 0.0f;
    };

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

        // VK-296: Per-streaming-unit tracking for granular eviction
        std::vector<StreamingUnitState> unitStates;
    };

    // =========================================================================
    // ClusterMeshState - Per-mesh tracking
    // =========================================================================

    struct ClusterMeshState
    {
        std::unique_ptr<resource::MeshStreamHandle> handle;
        uint32_t referenceCount = 0;
        uint32_t pendingAsyncReads = 0;  // Track in-flight async operations to prevent use-after-free
        bool headerParsed = false;
        std::unordered_map<std::string, ClusterSubmeshState> submeshStates;  // key: submeshName#submeshIdx

        // THREAD SAFETY: Mesh state can only be erased when both conditions are met:
        // 1. referenceCount == 0 (no external references)
        // 2. pendingAsyncReads == 0 (no in-flight async operations)
        bool canBeErased() const { return referenceCount == 0 && pendingAsyncReads == 0; }
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
        gpudriven::MergedMeshBuffer* mergedMeshBuffer = nullptr;  // VK-300: For marking cluster availability

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

        // Priority update optimization (VK-298 performance fix)
        // Avoid O(n log n) rebuild every frame by throttling updates
        uint64_t lastPriorityUpdateFrame = 0;
        glm::vec3 lastCameraPos{0.0f};
        uint32_t priorityUpdateFrameInterval = 15;   // Update at most every N frames
        float cameraMovementThreshold = 5.0f;        // Force update if camera moves this far

        // VK-296: Eviction configuration and statistics
        EvictionConfig evictionConfig;
        EvictionStats evictionStats;

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
        void setMergedMeshBuffer(gpudriven::MergedMeshBuffer* buffer) { mergedMeshBuffer = buffer; }
        void setMaxGPUMemoryBudget(size_t bytes) { maxGPUMemoryBudget = bytes; }
        void setMaxBytesPerFrame(size_t bytes) { maxBytesPerFrame = bytes; }
        void setMaxPendingReads(uint32_t count) { maxPendingReads = count; }
        void setMaxPendingUploads(uint32_t count) { maxPendingUploads = count; }

        // VK-296: Eviction configuration
        void setEvictionConfig(const EvictionConfig& config) { evictionConfig = config; }
        const EvictionConfig& getEvictionConfig() const { return evictionConfig; }
        void setRecencyProtectionFrames(uint32_t frames) { evictionConfig.recencyProtectionFrames = frames; }
        void setEvictionWeights(float recency, float screenError, float memorySize)
        {
            evictionConfig.recencyWeight = recency;
            evictionConfig.screenErrorWeight = screenError;
            evictionConfig.memorySizeWeight = memorySize;
        }
        void enableGroupEviction(bool enable) { evictionConfig.enableGroupEviction = enable; }
        void enableDependencyRespect(bool enable) { evictionConfig.respectDependencies = enable; }
        const EvictionStats& getEvictionStats() const { return evictionStats; }

        // Priority update throttling (VK-298 performance optimization)
        void setPriorityUpdateInterval(uint32_t frames) { priorityUpdateFrameInterval = frames; }
        void setCameraMovementThreshold(float distance) { cameraMovementThreshold = distance; }
        uint32_t getPriorityUpdateInterval() const { return priorityUpdateFrameInterval; }
        float getCameraMovementThreshold() const { return cameraMovementThreshold; }

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

        // Type alias for lock guard - used to enforce lock holding at compile time
        using MeshStatesLock = std::unique_lock<std::mutex>;

        // Mesh stream management
        // THREAD SAFETY: These functions require meshStatesMutex to be held.
        // The lock parameter enforces this at compile time - callers must pass
        // a valid unique_lock, ensuring they have acquired the mutex.
        void openMeshStreamLocked(const std::string& meshPath, const MeshStatesLock& lock);
        void scheduleInitialClustersLocked(const std::string& meshPath, const MeshStatesLock& lock);

        // Streaming pipeline
        void processStreamingQueue();
        void processPendingReads();
        void processPendingUploads();

        // Priority management
        void updatePriorities(const glm::vec3& cameraPos);
        float calculatePriority(const ClusterStreamingRequest& request,
                               const glm::vec3& cameraPos) const;

        // Eviction (VK-296: Enhanced with group strategy)
        void evictLRUClustersIfNeeded(size_t requiredBytes);
        void evictWithGroupStrategy(size_t requiredBytes);
        std::vector<EvictionCandidate> buildEvictionCandidates() const;
        void filterByDependencies(std::vector<EvictionCandidate>& candidates) const;
        std::vector<std::vector<EvictionCandidate>> groupAdjacentCandidates(
            const std::vector<EvictionCandidate>& candidates) const;
        size_t evictGroup(const std::vector<EvictionCandidate>& group);
        float calculateEvictionPriority(const StreamingUnitState& state) const;
        bool isProtectedFromEviction(const StreamingUnitState& state) const;
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
