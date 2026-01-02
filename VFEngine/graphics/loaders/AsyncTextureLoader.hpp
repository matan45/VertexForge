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

    private:
        static constexpr float PROGRESS_LOADING_STARTED = 0.1f;
        static constexpr float PROGRESS_CPU_COMPLETE = 0.5f;
        static constexpr float PROGRESS_COMPLETE = 1.0f;

        mutable std::mutex mutex;
        std::unordered_map<void*, std::unique_ptr<PendingTextureLoad>> pendingLoads;
        void* gpuUploadReadyInstance = nullptr; // Protected by mutex
        
    public:
        explicit AsyncTextureLoader() = default;
        ~AsyncTextureLoader() = default;

        // Non-copyable
        AsyncTextureLoader(const AsyncTextureLoader&) = delete;
        AsyncTextureLoader& operator=(const AsyncTextureLoader&) = delete;

        void startLoad(void* instanceId, const std::string& texturePath, bool isHDR);
        void cancelLoad(void* instanceId);
        bool update();

        void* getReadyForGPUUpload() const;
        bool processGPUUpload(void* instanceId);

        services::TextureLoadingProgress getProgress(void* instanceId) const;

        bool isLoadComplete(void* instanceId) const;

        std::unique_ptr<dto::EditorTexture> takeTexture(void* instanceId);

        // Remove entries in terminal states (Error, Cancelled) to prevent memory leaks
        void clearFinishedLoads();
    };
}
