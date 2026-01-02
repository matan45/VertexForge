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

    private:
        mutable std::mutex mutex;
        std::unordered_map<std::string, std::unique_ptr<PendingMeshLoad>> pendingLoads;
        std::string gpuUploadReadyPath; // Path of mesh ready for GPU upload
    public:
        struct LoadResult
        {
            bool success = false;
            std::string meshId;
            math::AABB bounds;
            std::string errorMessage;
        };

        explicit AsyncMeshLoader() = default;
        ~AsyncMeshLoader() = default;

        // Non-copyable
        AsyncMeshLoader(const AsyncMeshLoader&) = delete;
        AsyncMeshLoader& operator=(const AsyncMeshLoader&) = delete;

        void startLoad(const std::string& meshPath);

        void cancelLoad(const std::string& meshPath);

        bool update();

        LoadResult processGPUUpload(render::mesh::StaticMeshPipeline* pipeline);

        services::MeshLoadingProgress getProgress(const std::string& meshPath) const;

        void clearCompleted();
    };
}
