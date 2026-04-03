#pragma once
#include "../../services/data/AsyncLoadingTypes.hpp"
#include "resource/Types.hpp"
#include "resource/ResourceLoadTypes.hpp"
#include "resource/CancellationToken.hpp"
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
            resource::CancellationToken::Ptr cancellation = resource::CancellationToken::create();
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

        void startLoad(const std::string& meshPath, const resource::LoadHint& hint = {});

        void cancelLoad(const std::string& meshPath);

        bool update();

        LoadResult processGPUUpload(render::mesh::StaticMeshPipeline* pipeline);

        services::MeshLoadingProgress getProgress(const std::string& meshPath) const;

        // Remove entries in terminal states (Error, Cancelled) to prevent memory leaks
        // Note: Complete entries are auto-cleaned in processGPUUpload
        void clearFinishedLoads();
    };
}
