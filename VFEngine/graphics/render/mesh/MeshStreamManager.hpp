#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <future>
#include <mutex>
#include <glm/glm.hpp>
#include "../gpudriven/GPUDrivenTypes.hpp"

namespace core {
    class Device;
}

namespace resource {
    class MeshStreamHandle;
    struct Vertex;
}

namespace render::gpudriven {
    class MergedMeshBuffer;
}

namespace render::mesh {

    // Streaming request for a single LOD level
    struct StreamingRequest {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex;
        uint32_t lodLevel;
        float priority;

        // For priority updates
        glm::vec3 worldCenter;
        float boundingRadius;

        // Comparison for priority queue (higher priority = processed first)
        bool operator<(const StreamingRequest& other) const {
            return priority < other.priority;
        }
    };

    // Result of async LOD read
    struct StreamingResult {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex;
        uint32_t lodLevel;
        std::vector<resource::Vertex> vertices;
        std::vector<uint32_t> indices;
        bool success;
    };

    // State for a mesh being streamed
    struct MeshStreamingState {
        std::unique_ptr<resource::MeshStreamHandle> handle;
        uint32_t referenceCount = 0;
        bool headerParsed = false;
    };

    // Mesh streaming manager - orchestrates LOD streaming with priority queue
    class MeshStreamManager {
    public:
        explicit MeshStreamManager(core::Device& device,
                                    gpudriven::MergedMeshBuffer& mergedBuffer);
        ~MeshStreamManager();

        // Non-copyable
        MeshStreamManager(const MeshStreamManager&) = delete;
        MeshStreamManager& operator=(const MeshStreamManager&) = delete;

        // Request a mesh for streaming (called when entity added to scene)
        // Returns true if mesh is being tracked (may not be ready yet)
        bool requestMesh(const std::string& meshPath);

        // Release reference to mesh (called when entity removed)
        void releaseMesh(const std::string& meshPath);

        // Update streaming - call each frame
        // Processes priority queue, uploads data to GPU
        void update(const glm::vec3& cameraPos,
                    const glm::mat4& viewProj,
                    float deltaTime);

        // Wait for all pending transfers to complete
        void waitForPendingTransfers();

        // Check if mesh has any renderable LOD
        bool isMeshRenderable(const std::string& meshPath) const;

        // Get best available LOD for submesh
        uint32_t getBestAvailableLOD(const std::string& meshPath,
                                      const std::string& submeshName,
                                      uint32_t submeshIndex,
                                      uint32_t preferredLOD) const;

        // Configuration
        void setMaxBytesPerFrame(size_t bytes) { maxBytesPerFrame = bytes; }
        void setMaxPendingReads(uint32_t count) { maxPendingReads = count; }
        void setMaxPendingUploads(uint32_t count) { maxPendingUploads = count; }

        // Statistics
        struct Stats {
            uint32_t meshesTracked = 0;
            uint32_t meshesReady = 0;
            uint32_t lodsQueued = 0;
            uint32_t lodsStreaming = 0;
            uint32_t lodsUploading = 0;
            size_t bytesStreamedThisFrame = 0;
            size_t totalBytesStreamed = 0;
        };
        const Stats& getStats() const { return stats; }

    private:
        core::Device& device;
        gpudriven::MergedMeshBuffer& mergedBuffer;

        // Active mesh streams
        std::unordered_map<std::string, MeshStreamingState> meshStates;
        mutable std::mutex meshStatesMutex;

        // Priority queue for LOD streaming
        std::priority_queue<StreamingRequest> streamingQueue;

        // Currently reading (async file I/O)
        std::vector<std::future<StreamingResult>> pendingReads;

        // Waiting for GPU upload confirmation
        struct PendingUpload {
            std::string meshPath;
            std::string submeshName;
            uint32_t submeshIndex;
            uint32_t lodLevel;
        };
        std::vector<PendingUpload> pendingUploads;

        // Configuration
        size_t maxBytesPerFrame = 4 * 1024 * 1024;  // 4MB per frame
        uint32_t maxPendingReads = 4;
        uint32_t maxPendingUploads = 8;

        // Frame tracking
        size_t bytesStreamedThisFrame = 0;

        Stats stats{};

        // Internal methods
        void openMeshStream(const std::string& meshPath);
        void scheduleInitialLODs(const std::string& meshPath);
        void processStreamingQueue();
        void processPendingReads();
        void processPendingUploads();
        void updatePriorities(const glm::vec3& cameraPos, const glm::mat4& viewProj);

        // Priority calculation
        float calculatePriority(const StreamingRequest& request,
                                const glm::vec3& cameraPos,
                                const glm::mat4& viewProj) const;

        // Async read a LOD from file
        std::future<StreamingResult> asyncReadLOD(const std::string& meshPath,
                                                   const std::string& submeshName,
                                                   uint32_t submeshIndex,
                                                   uint32_t lodLevel);
    };

}
