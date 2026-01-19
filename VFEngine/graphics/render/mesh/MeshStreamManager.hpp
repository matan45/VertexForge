#pragma once

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
