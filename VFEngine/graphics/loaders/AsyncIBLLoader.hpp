#pragma once
#include "../../services/data/AsyncLoadingTypes.hpp"
#include "resource/Types.hpp"
#include <memory>
#include <string>
#include <future>
#include <mutex>
#include <atomic>

namespace core
{
    class Device;
    class Texture;
}

namespace render
{
    class IBL;
}

namespace loaders
{
    class AsyncIBLLoader
    {
    public:
        using Stage = services::IBLLoadingProgress::Stage;

        struct IBLLoadState
        {
            std::string hdrPath;
            services::LoadingState state = services::LoadingState::Idle;
            Stage currentStage = Stage::HDRFile;
            float progress = 0.0f;
            std::string statusMessage;
            std::string errorMessage;
            std::atomic<bool> cancelled{false};

            // Async HDR loading
            std::future<std::shared_ptr<resource::HDRData>> hdrFuture;
            std::shared_ptr<resource::HDRData> hdrData;

            // Texture created from HDR data
            std::shared_ptr<core::Texture> hdrTexture;
        };

        explicit AsyncIBLLoader(core::Device& device);
        ~AsyncIBLLoader() = default;

        // Non-copyable
        AsyncIBLLoader(const AsyncIBLLoader&) = delete;
        AsyncIBLLoader& operator=(const AsyncIBLLoader&) = delete;

        // Start async IBL loading
        void startLoad(const std::string& hdrPath);

        // Cancel the current load
        void cancelLoad();

        // Check if there's an active load
        bool hasActiveLoad() const;

        // Process one frame of work (call each frame)
        // Returns true when fully complete (success or error)
        bool processFrame(render::IBL* ibl);

        // Get current loading progress
        services::IBLLoadingProgress getProgress() const;

        // Check if IBL is ready for use
        bool isComplete() const;

        // Check if loading failed
        bool hasError() const;

        // Get error message if loading failed
        std::string getErrorMessage() const;

        // Clear the load state after completion
        void clearState();

    private:
        core::Device& device;
        mutable std::mutex mutex;
        std::unique_ptr<IBLLoadState> loadState;

        // Stage processing helpers
        bool processHDRFileStage();
        bool processEnvironmentCubemapStage(render::IBL* ibl);
        bool processIrradianceStage(render::IBL* ibl);
        bool processBRDFStage(render::IBL* ibl);
        bool processPrefilteredStage(render::IBL* ibl);
        bool processSkyboxStage(render::IBL* ibl);

        void updateProgress();
    };
}
