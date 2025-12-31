#pragma once
#include "../../services/data/AsyncLoadingTypes.hpp"
#include "resource/Types.hpp"
#include "math/Frustum.hpp"
#include <memory>
#include <string>
#include <future>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace render::mesh
{
    class StaticMeshPipeline;
}

namespace loaders
{
    class AsyncMeshLoader
    {
    public:
        struct PendingMeshLoad
        {
            std::string meshPath;
            std::future<std::shared_ptr<resource::MeshesData>> cpuDataFuture;
            std::shared_ptr<resource::MeshesData> cpuData;
            services::LoadingState state = services::LoadingState::Pending;
            float progress = 0.0f;
            std::string statusMessage = "Queued...";
            std::string errorMessage;
            std::atomic<bool> cancelled{false};
        };

        struct LoadResult
        {
            bool success = false;
            std::string meshId;
            math::AABB bounds;
            std::string errorMessage;
        };

        AsyncMeshLoader() = default;
        ~AsyncMeshLoader() = default;

        // Non-copyable
        AsyncMeshLoader(const AsyncMeshLoader&) = delete;
        AsyncMeshLoader& operator=(const AsyncMeshLoader&) = delete;

        // Start async file loading for a mesh
        void startLoad(const std::string& meshPath);

        // Cancel a pending load
        void cancelLoad(const std::string& meshPath);

        // Check if there's a pending load for this path
        bool hasPendingLoad(const std::string& meshPath) const;

        // Check/update loading state (call each frame)
        // Returns true if there's GPU work ready to be done
        bool update();

        // Get the path of the mesh that's ready for GPU upload
        // Returns empty string if none ready
        std::string getReadyForGPUUpload() const;

        // Do GPU upload work (call from main thread when update() indicates ready)
        // Returns the result of the upload operation
        LoadResult processGPUUpload(render::mesh::StaticMeshPipeline* pipeline);

        // Get loading progress for a specific mesh
        services::MeshLoadingProgress getProgress(const std::string& meshPath) const;

        // Check if loading is complete for a specific mesh
        bool isLoadComplete(const std::string& meshPath) const;

        // Clear completed/cancelled loads
        void clearCompleted();

    private:
        mutable std::mutex mutex;
        std::unordered_map<std::string, std::unique_ptr<PendingMeshLoad>> pendingLoads;
        std::string gpuUploadReadyPath;  // Path of mesh ready for GPU upload
    };
}
