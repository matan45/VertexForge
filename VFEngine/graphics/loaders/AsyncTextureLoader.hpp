#pragma once
#include "../../services/data/AsyncLoadingTypes.hpp"
#include "resource/Types.hpp"
#include <memory>
#include <string>
#include <future>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <variant>

namespace core
{
    class Device;
    class Texture;
}

namespace dto
{
    class EditorTexture;
}

namespace loaders
{
    class AsyncTextureLoader
    {
    public:
        using TextureDataVariant = std::variant<resource::TextureData, resource::HDRData>;

        struct PendingTextureLoad
        {
            void* instanceId = nullptr;
            std::string texturePath;
            bool isHDR = false;
            std::future<TextureDataVariant> cpuDataFuture;
            std::shared_ptr<TextureDataVariant> cpuData;
            services::LoadingState state = services::LoadingState::Pending;
            float progress = 0.0f;
            std::string statusMessage = "Queued...";
            std::string errorMessage;
            std::atomic<bool> cancelled{false};

            // Result after GPU upload
            std::unique_ptr<dto::EditorTexture> texture;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        AsyncTextureLoader() = default;
        ~AsyncTextureLoader() = default;

        // Non-copyable
        AsyncTextureLoader(const AsyncTextureLoader&) = delete;
        AsyncTextureLoader& operator=(const AsyncTextureLoader&) = delete;

        // Start async file loading for a texture
        void startLoad(void* instanceId, const std::string& texturePath, bool isHDR);

        // Cancel a pending load
        void cancelLoad(void* instanceId);

        // Check if there's a pending load for this instance
        bool hasPendingLoad(void* instanceId) const;

        // Check/update loading state (call each frame)
        // Returns true if there's GPU work ready to be done
        bool update();

        // Get the instance that's ready for GPU upload
        // Returns nullptr if none ready
        void* getReadyForGPUUpload() const;

        // Do GPU upload work (call from main thread when update() indicates ready)
        // Returns true on success
        bool processGPUUpload(void* instanceId);

        // Get loading progress for a specific instance
        services::TextureLoadingProgress getProgress(void* instanceId) const;

        // Check if loading is complete for a specific instance
        bool isLoadComplete(void* instanceId) const;

        // Get the loaded texture (after GPU upload complete)
        std::unique_ptr<dto::EditorTexture> takeTexture(void* instanceId);

        // Get width/height after load
        uint32_t getWidth(void* instanceId) const;
        uint32_t getHeight(void* instanceId) const;

        // Clear completed/cancelled loads
        void clearCompleted();

    private:
        // All member access must be protected by mutex.
        // gpuUploadReadyInstance tracks which instance is next for GPU upload.
        mutable std::mutex mutex;
        std::unordered_map<void*, std::unique_ptr<PendingTextureLoad>> pendingLoads;
        void* gpuUploadReadyInstance = nullptr;  // Protected by mutex
    };
}
