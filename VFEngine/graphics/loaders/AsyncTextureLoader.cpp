#include "AsyncTextureLoader.hpp"
#include "resource/TextureResource.hpp"
#include "resource/ResourceManager.hpp"
#include "../../core/controllers/EditorTextureController.hpp"
#include "../../core/controllers/texture/EditorTexture.hpp"
#include "print/Log.hpp"
#include <vector>

namespace loaders
{
    void AsyncTextureLoader::startLoad(void* instanceId, const std::string& texturePath, bool isHDR, const resource::LoadHint& hint)
    {
        std::lock_guard<std::mutex> lock(mutex);

        // Check if already loading for this instance
        if (pendingLoads.find(instanceId) != pendingLoads.end())
        {
            vfLogWarning("AsyncTextureLoader: Already loading texture for instance {:p}", instanceId);
            return;
        }

        auto pending = std::make_unique<PendingTextureLoad>();
        pending->instanceId = instanceId;
        pending->texturePath = texturePath;
        pending->isHDR = isHDR;
        pending->state = services::LoadingState::Loading;
        pending->statusMessage = isHDR ? "Loading HDR texture..." : "Loading texture...";
        pending->progress = PROGRESS_LOADING_STARTED;

        // Start async file I/O through the priority-aware ResourceManager
        auto cancellation = pending->cancellation;
        pending->cpuDataFuture = std::async(std::launch::async,
            [texturePath, isHDR, hint, cancel = cancellation]() -> TextureDataVariant {
            if (cancel && cancel->isCancelled())
            {
                if (isHDR) return resource::HDRData{};
                return resource::TextureData{};
            }
            if (isHDR)
            {
                return resource::TextureResource::loadHDR(texturePath);
            }
            else
            {
                return resource::TextureResource::loadTexture(texturePath);
            }
        });

        pendingLoads[instanceId] = std::move(pending);
        vfLogDebug("AsyncTextureLoader: Started async load for {} (HDR: {})", texturePath, isHDR);
    }

    void AsyncTextureLoader::cancelLoad(void* instanceId)
    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = pendingLoads.find(instanceId);
        if (it == pendingLoads.end())
        {
            return;
        }

        it->second->cancellation->cancel();
        it->second->state = services::LoadingState::Cancelled;
        it->second->statusMessage = "Cancelled";

        if (gpuUploadReadyInstance == instanceId)
        {
            gpuUploadReadyInstance = nullptr;
        }
    }

    bool AsyncTextureLoader::update()
    {
        std::lock_guard<std::mutex> lock(mutex);

        bool hasGPUWork = false;

        // Collect cancelled entries to erase (can't erase during range-for)
        std::vector<void*> toErase;

        for (auto& [id, pending] : pendingLoads)
        {
            if (pending->cancellation->isCancelled())
            {
                // Check if future is ready so we can safely erase
                // (erasing with running future would block in destructor)
                if (!pending->cpuDataFuture.valid() ||
                    pending->cpuDataFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
                {
                    toErase.push_back(id);
                }
                continue;
            }

            if (pending->state == services::LoadingState::Loading)
            {
                // Check if future is ready (non-blocking)
                if (pending->cpuDataFuture.valid() &&
                    pending->cpuDataFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
                {
                    try
                    {
                        auto data = pending->cpuDataFuture.get();
                        pending->cpuData = std::make_shared<TextureDataVariant>(std::move(data));
                        pending->state = services::LoadingState::GPUUploadPending;
                        pending->statusMessage = "Creating GPU texture...";
                        pending->progress = PROGRESS_CPU_COMPLETE;

                        if (!hasGPUWork && gpuUploadReadyInstance == nullptr)
                        {
                            gpuUploadReadyInstance = id;
                            hasGPUWork = true;
                        }
                    }
                    catch (const std::exception& e)
                    {
                        pending->state = services::LoadingState::Error;
                        pending->errorMessage = e.what();
                        pending->statusMessage = "Failed to load texture";
                        vfLogError("AsyncTextureLoader: Failed to load {}: {}", pending->texturePath, e.what());
                    }
                }
            }
            else if (pending->state == services::LoadingState::GPUUploadPending)
            {
                if (!hasGPUWork && gpuUploadReadyInstance == nullptr)
                {
                    gpuUploadReadyInstance = id;
                    hasGPUWork = true;
                }
            }
        }

        // Erase cancelled entries whose futures are complete
        for (void* id : toErase)
        {
            if (gpuUploadReadyInstance == id)
            {
                gpuUploadReadyInstance = nullptr;
            }
            pendingLoads.erase(id);
        }

        return hasGPUWork;
    }

    void* AsyncTextureLoader::getReadyForGPUUpload() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return gpuUploadReadyInstance;
    }

    bool AsyncTextureLoader::processGPUUpload(void* instanceId)
    {
        std::unique_lock<std::mutex> lock(mutex);

        auto it = pendingLoads.find(instanceId);
        if (it == pendingLoads.end())
        {
            return false;
        }

        PendingTextureLoad* pending = it->second.get();

        if (pending->state != services::LoadingState::GPUUploadPending || !pending->cpuData)
        {
            return false;
        }

        if (pending->cancellation->isCancelled())
        {
            if (gpuUploadReadyInstance == instanceId)
            {
                gpuUploadReadyInstance = nullptr;
            }
            pendingLoads.erase(it);
            return false;
        }

        // Extract data needed for GPU upload before releasing lock
        std::string texturePath = pending->texturePath;
        bool isHDR = pending->isHDR;
        auto cpuData = pending->cpuData;

        // Release lock during GPU operations to avoid blocking other async operations
        lock.unlock();

        std::unique_ptr<dto::EditorTexture> texture;
        std::string errorMessage;

        try
        {
            // Perform GPU upload without holding the lock
            if (isHDR)
            {
                const auto& hdrData = std::get<resource::HDRData>(*cpuData);
                texture = controllers::EditorTextureController::loadHdrTextureFromData(hdrData);
            }
            else
            {
                const auto& textureData = std::get<resource::TextureData>(*cpuData);
                texture = controllers::EditorTextureController::loadTextureFromData(textureData);
            }
        }
        catch (const std::exception& e)
        {
            errorMessage = e.what();
        }

        // Re-acquire lock to update state
        lock.lock();

        // Re-find the pending load (it may have been cancelled/removed during unlock)
        it = pendingLoads.find(instanceId);
        if (it == pendingLoads.end())
        {
            vfLogWarning("AsyncTextureLoader: Load was removed during GPU upload for {}", texturePath);
            return false;
        }

        pending = it->second.get();

        // Check if cancelled during GPU upload
        if (pending->cancellation->isCancelled())
        {
            if (gpuUploadReadyInstance == instanceId)
            {
                gpuUploadReadyInstance = nullptr;
            }
            pendingLoads.erase(it);
            vfLogDebug("AsyncTextureLoader: Load was cancelled during GPU upload for {}", texturePath);
            return false;
        }

        if (!errorMessage.empty())
        {
            pending->state = services::LoadingState::Error;
            pending->errorMessage = errorMessage;
            pending->statusMessage = "GPU upload failed";
            vfLogError("AsyncTextureLoader: GPU upload failed for {}: {}", texturePath, errorMessage);

            if (gpuUploadReadyInstance == instanceId)
            {
                gpuUploadReadyInstance = nullptr;
            }
            return false;
        }

        if (!texture)
        {
            pending->state = services::LoadingState::Error;
            pending->errorMessage = "Failed to create GPU texture";
            pending->statusMessage = "GPU texture creation failed";
            vfLogError("AsyncTextureLoader: Failed to create GPU texture for {}", texturePath);

            if (gpuUploadReadyInstance == instanceId)
            {
                gpuUploadReadyInstance = nullptr;
            }
            return false;
        }

        pending->width = texture->getWidth();
        pending->height = texture->getHeight();
        pending->texture = std::move(texture);
        pending->state = services::LoadingState::Complete;
        pending->statusMessage = "Complete";
        pending->progress = PROGRESS_COMPLETE;
        pending->cpuData.reset();  // Release CPU data now that GPU texture is created

        if (gpuUploadReadyInstance == instanceId)
        {
            gpuUploadReadyInstance = nullptr;
        }

        vfLogDebug("AsyncTextureLoader: GPU upload complete for {}", texturePath);
        return true;
    }

    services::TextureLoadingProgress AsyncTextureLoader::getProgress(void* instanceId) const
    {
        std::lock_guard<std::mutex> lock(mutex);

        services::TextureLoadingProgress progress;

        auto it = pendingLoads.find(instanceId);
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
        progress.width = pending->width;
        progress.height = pending->height;
        progress.isHDR = pending->isHDR;

        return progress;
    }

    bool AsyncTextureLoader::isLoadComplete(void* instanceId) const
    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = pendingLoads.find(instanceId);
        if (it == pendingLoads.end())
        {
            return false;
        }

        return it->second->state == services::LoadingState::Complete;
    }

    std::unique_ptr<dto::EditorTexture> AsyncTextureLoader::takeTexture(void* instanceId)
    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = pendingLoads.find(instanceId);
        if (it == pendingLoads.end())
        {
            return nullptr;
        }

        if (it->second->state != services::LoadingState::Complete)
        {
            return nullptr;
        }

        auto texture = std::move(it->second->texture);

        // Auto-remove completed entry to prevent memory accumulation
        if (gpuUploadReadyInstance == instanceId)
        {
            gpuUploadReadyInstance = nullptr;
        }
        pendingLoads.erase(it);

        return texture;
    }

    void AsyncTextureLoader::clearFinishedLoads()
    {
        std::lock_guard<std::mutex> lock(mutex);

        std::erase_if(pendingLoads, [this](const auto& pair) {
            const auto& pending = pair.second;
            bool shouldRemove = pending->state == services::LoadingState::Error ||
                                pending->state == services::LoadingState::Cancelled;

            if (shouldRemove && gpuUploadReadyInstance == pair.first)
            {
                gpuUploadReadyInstance = nullptr;
            }

            return shouldRemove;
        });
    }
}
