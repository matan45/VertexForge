#include "AsyncMeshLoader.hpp"
#include "../render/mesh/StaticMeshPipeline.hpp"
#include "resource/ResourceManager.hpp"
#include "math/Frustum.hpp"
#include "print/Logger.hpp"
#include <chrono>

namespace loaders
{
    void AsyncMeshLoader::startLoad(const std::string& meshPath)
    {
        std::lock_guard lock(mutex);

        // Check if already loading
        if (pendingLoads.find(meshPath) != pendingLoads.end())
        {
            loggerInfo("Mesh already being loaded: {}", meshPath);
            return;
        }

        auto pending = std::make_unique<PendingMeshLoad>();
        pending->meshPath = meshPath;
        pending->state = services::LoadingState::Loading;
        pending->progress = 0.0f;
        pending->statusMessage = "Loading mesh from disk...";

        // Start async file loading using ResourceManager
        pending->cpuDataFuture = resource::ResourceManager::loadMeshAsync(meshPath);

        pendingLoads[meshPath] = std::move(pending);

        loggerInfo("Started async mesh load: {}", meshPath);
    }

    void AsyncMeshLoader::cancelLoad(const std::string& meshPath)
    {
        std::lock_guard lock(mutex);

        auto it = pendingLoads.find(meshPath);
        if (it != pendingLoads.end())
        {
            it->second->cancelled = true;
            it->second->state = services::LoadingState::Cancelled;
            it->second->statusMessage = "Cancelled";
            loggerInfo("Cancelled mesh load: {}", meshPath);
        }
    }

    bool AsyncMeshLoader::hasPendingLoad(const std::string& meshPath) const
    {
        std::lock_guard lock(mutex);
        return pendingLoads.find(meshPath) != pendingLoads.end();
    }

    bool AsyncMeshLoader::update()
    {
        std::lock_guard lock(mutex);

        gpuUploadReadyPath.clear();

        for (auto& [path, pending] : pendingLoads)
        {
            if (pending->cancelled)
            {
                continue;
            }

            if (pending->state == services::LoadingState::Loading)
            {
                // Check if the async future is ready (non-blocking check)
                if (pending->cpuDataFuture.valid())
                {
                    auto status = pending->cpuDataFuture.wait_for(std::chrono::milliseconds(0));
                    if (status == std::future_status::ready)
                    {
                        try
                        {
                            pending->cpuData = pending->cpuDataFuture.get();
                            if (pending->cpuData)
                            {
                                pending->state = services::LoadingState::GPUUploadPending;
                                pending->progress = 0.5f;
                                pending->statusMessage = "Uploading to GPU...";
                                gpuUploadReadyPath = path;
                                loggerInfo("Mesh loaded from disk, ready for GPU upload: {}", path);
                            }
                            else
                            {
                                pending->state = services::LoadingState::Error;
                                pending->errorMessage = "Failed to load mesh data from file";
                                pending->statusMessage = "Error: Failed to load";
                                loggerError("Failed to load mesh data: {}", path);
                            }
                        }
                        catch (const std::exception& e)
                        {
                            pending->state = services::LoadingState::Error;
                            pending->errorMessage = e.what();
                            pending->statusMessage = "Error: " + std::string(e.what());
                            loggerError("Exception loading mesh {}: {}", path, e.what());
                        }
                    }
                    else
                    {
                        // Still loading, update progress estimate
                        pending->progress = std::min(0.45f, pending->progress + 0.01f);
                    }
                }
            }
        }

        return !gpuUploadReadyPath.empty();
    }

    std::string AsyncMeshLoader::getReadyForGPUUpload() const
    {
        std::lock_guard lock(mutex);
        return gpuUploadReadyPath;
    }

    AsyncMeshLoader::LoadResult AsyncMeshLoader::processGPUUpload(render::mesh::StaticMeshPipeline* pipeline)
    {
        LoadResult result;

        std::unique_lock lock(mutex);

        if (gpuUploadReadyPath.empty())
        {
            result.errorMessage = "No mesh ready for GPU upload";
            return result;
        }

        auto it = pendingLoads.find(gpuUploadReadyPath);
        if (it == pendingLoads.end())
        {
            result.errorMessage = "Pending load not found";
            return result;
        }

        PendingMeshLoad* pending = it->second.get();

        if (pending->cancelled)
        {
            result.errorMessage = "Load was cancelled";
            pending->state = services::LoadingState::Cancelled;
            gpuUploadReadyPath.clear();
            return result;
        }

        if (!pending->cpuData)
        {
            result.errorMessage = "No CPU data available";
            pending->state = services::LoadingState::Error;
            gpuUploadReadyPath.clear();
            return result;
        }

        std::string meshPath = pending->meshPath;
        auto cpuData = pending->cpuData;

        // Release lock during GPU operation
        lock.unlock();

        // Perform GPU upload (this is on the main thread)
        std::string meshId = pipeline->uploadMesh(meshPath, *cpuData);

        lock.lock();

        // Re-find the pending load (it may have been cancelled/removed during unlock)
        it = pendingLoads.find(meshPath);
        if (it == pendingLoads.end())
        {
            result.errorMessage = "Load was removed during GPU upload";
            gpuUploadReadyPath.clear();
            return result;
        }

        pending = it->second.get();

        // Check if cancelled during GPU upload
        if (pending->cancelled)
        {
            result.errorMessage = "Load was cancelled during GPU upload";
            pending->state = services::LoadingState::Cancelled;
            gpuUploadReadyPath.clear();
            loggerInfo("Mesh load cancelled during GPU upload: {}", meshPath);
            return result;
        }

        if (meshId.empty())
        {
            pending->state = services::LoadingState::Error;
            pending->errorMessage = "Failed to upload mesh to GPU";
            pending->statusMessage = "Error: GPU upload failed";
            result.errorMessage = pending->errorMessage;
            gpuUploadReadyPath.clear();
            return result;
        }

        // Get bounding box
        const math::AABB* bounds = pipeline->getMeshBoundingBox(meshPath);
        if (bounds)
        {
            result.bounds = *bounds;
        }
        else
        {
            result.bounds = math::AABB{glm::vec3(-1.0f), glm::vec3(1.0f)};
        }

        pending->state = services::LoadingState::Complete;
        pending->progress = 1.0f;
        pending->statusMessage = "Complete";

        result.success = true;
        result.meshId = meshId;

        gpuUploadReadyPath.clear();

        loggerInfo("Mesh GPU upload complete: {}", meshPath);

        return result;
    }

    services::MeshLoadingProgress AsyncMeshLoader::getProgress(const std::string& meshPath) const
    {
        std::lock_guard lock(mutex);

        services::MeshLoadingProgress progress;

        auto it = pendingLoads.find(meshPath);
        if (it == pendingLoads.end())
        {
            progress.state = services::LoadingState::Idle;
            return progress;
        }

        const auto& pending = it->second;
        progress.state = pending->state;
        progress.progress = pending->progress;
        progress.statusMessage = pending->statusMessage;
        progress.errorMessage = pending->errorMessage;

        return progress;
    }

    bool AsyncMeshLoader::isLoadComplete(const std::string& meshPath) const
    {
        std::lock_guard lock(mutex);

        auto it = pendingLoads.find(meshPath);
        if (it == pendingLoads.end())
        {
            return false;
        }

        return it->second->state == services::LoadingState::Complete;
    }

    void AsyncMeshLoader::clearCompleted()
    {
        std::lock_guard lock(mutex);

        for (auto it = pendingLoads.begin(); it != pendingLoads.end();)
        {
            if (it->second->state == services::LoadingState::Complete ||
                it->second->state == services::LoadingState::Error ||
                it->second->state == services::LoadingState::Cancelled)
            {
                it = pendingLoads.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
