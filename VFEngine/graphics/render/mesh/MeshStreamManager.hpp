#pragma once

#include <array>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <future>
#include <mutex>
#include <glm/glm.hpp>
#include "../gpudriven/GPUDrivenTypes.hpp"

namespace core
{
    class Device;
}

namespace resource
{
    class MeshStreamHandle;
    struct Vertex;
}

namespace render::gpudriven
{
    class MergedMeshBuffer;
    class MeshletBuffer;
}

namespace render::mesh
{
    struct StreamingRequest
    {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex;
        uint32_t lodLevel;
        float priority;
        glm::vec3 worldCenter;
        float boundingRadius;

        bool operator<(const StreamingRequest& other) const
        {
            return priority < other.priority;
        }
    };

    struct StreamingResult
    {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex;
        uint32_t lodLevel;
        std::vector<resource::Vertex> vertices;
        std::vector<uint32_t> indices;
        bool success;
    };

    struct MeshStreamingState
    {
        std::unique_ptr<resource::MeshStreamHandle> handle;
        uint32_t referenceCount = 0;
        bool headerParsed = false;

        // VK-1594: geometry that was pushed in from memory rather than streamed from a file.
        // There is no file to re-stream from, so unrequestMesh must not evict it - only an
        // explicit releaseInMemoryMesh may.
        bool inMemory = false;
    };

    // VK-1594: non-owning view of one LOD of an in-memory mesh. Valid only for the duration of
    // the registerInMemoryMesh call - the buffer is copied straight to the GPU.
    struct InMemoryLODData
    {
        const resource::Vertex* vertices = nullptr;
        uint32_t vertexCount = 0;
        const uint32_t* indices = nullptr;  // indices are LOD-local, not global
        uint32_t indexCount = 0;
    };

    struct InMemorySubmeshData
    {
        std::string name;
        std::array<InMemoryLODData, gpudriven::LOD_LEVEL_COUNT> lods;
    };

    struct InMemoryMeshData
    {
        std::string meshKey;
        std::vector<InMemorySubmeshData> submeshes;
    };

    class MeshStreamManager
    {
    private:
        core::Device& device;
        gpudriven::MergedMeshBuffer& mergedBuffer;
        gpudriven::MeshletBuffer* meshletBuffer = nullptr; 

        std::unordered_map<std::string, MeshStreamingState> meshStates;
        mutable std::mutex meshStatesMutex;

       
        std::priority_queue<StreamingRequest> streamingQueue;

        std::vector<std::future<StreamingResult>> pendingReads;
        mutable std::mutex pendingReadsMutex;

        struct PendingUpload
        {
            std::string meshPath;
            std::string submeshName;
            uint32_t submeshIndex;
            uint32_t lodLevel;
        };

        std::vector<PendingUpload> pendingUploads;

        size_t maxBytesPerFrame = 4 * 1024 * 1024; // 4MB per frame
        uint32_t maxPendingReads = 4;
        uint32_t maxPendingUploads = 8;

        size_t bytesStreamedThisFrame = 0;

    public:
        explicit MeshStreamManager(core::Device& device,
                                   gpudriven::MergedMeshBuffer& mergedBuffer);
        ~MeshStreamManager();
        MeshStreamManager(const MeshStreamManager&) = delete;
        MeshStreamManager& operator=(const MeshStreamManager&) = delete;

        void requestMesh(const std::string& meshPath);

        void unrequestMesh(const std::string& meshPath);

        // VK-1594: register geometry the caller already holds in RAM (baked HLOD proxies) under a
        // synthetic key, so it draws through the normal MeshComponent -> MergedMeshBuffer path.
        //
        // The state is created with headerParsed = true and no file handle, which is what makes
        // this work without touching the rest of the pipeline: update() only calls openMeshStream
        // for entries whose header is unparsed, scheduleInitialLODs early-outs on a null handle,
        // and GPUDrivenRenderer::updateMeshStreaming's unconditional per-frame requestMesh simply
        // bumps the refcount on the existing entry.
        //
        // Idempotent: re-registering an already-registered key is a no-op returning true.
        bool registerInMemoryMesh(const InMemoryMeshData& mesh);

        // Releases geometry registered by registerInMemoryMesh. Nothing will re-stream it.
        void releaseInMemoryMesh(const std::string& meshKey);

        [[nodiscard]] bool isInMemoryMesh(const std::string& meshKey) const;

        void update(const glm::vec3& cameraPos);

        void setMeshletBuffer(gpudriven::MeshletBuffer* buffer) { meshletBuffer = buffer; }

    private:
        void openMeshStream(const std::string& meshPath);
        void scheduleInitialLODs(const std::string& meshPath);
        void processStreamingQueue();
        void processPendingReads();
        void processPendingUploads();
        void updatePriorities(const glm::vec3& cameraPos);

        void handleCompletedRead(const StreamingResult& result);
        void queueHigherQualityLODs(const StreamingResult& result);

        float calculatePriority(const StreamingRequest& request,
                                const glm::vec3& cameraPos) const;

        std::future<StreamingResult> asyncReadLOD(const std::string& meshPath,
                                                  const std::string& submeshName,
                                                  uint32_t submeshIndex,
                                                  uint32_t lodLevel);
    };
}
