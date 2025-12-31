#pragma once
#include <string>
#include <cstdint>

namespace services
{
    enum class LoadingState
    {
        Idle,              // No loading in progress
        Pending,           // Request queued
        Loading,           // File I/O in progress (background thread)
        GPUUploadPending,  // File loaded, waiting for GPU upload
        Complete,          // Fully loaded and ready
        Error,             // Loading failed
        Cancelled          // Loading was cancelled
    };

    struct LoadingProgress
    {
        LoadingState state = LoadingState::Idle;
        float progress = 0.0f;           // 0.0 - 1.0
        std::string statusMessage;       // e.g., "Loading mesh...", "Uploading to GPU..."
        std::string errorMessage;        // Error details if state == Error

        bool isLoading() const
        {
            return state == LoadingState::Pending ||
                   state == LoadingState::Loading ||
                   state == LoadingState::GPUUploadPending;
        }

        bool isDone() const
        {
            return state == LoadingState::Complete ||
                   state == LoadingState::Error ||
                   state == LoadingState::Cancelled;
        }
    };

    struct MeshLoadingProgress : LoadingProgress
    {
        size_t bytesLoaded = 0;
        size_t totalBytes = 0;
    };

    struct TextureLoadingProgress : LoadingProgress
    {
        uint32_t width = 0;
        uint32_t height = 0;
        bool isHDR = false;
    };

    struct SceneLoadingProgress : LoadingProgress
    {
        size_t entitiesLoaded = 0;
        size_t totalEntities = 0;
        std::string currentEntityName;
    };

    struct IBLLoadingProgress : LoadingProgress
    {
        enum class Stage
        {
            HDRFile,            // 0-20%: Loading HDR file from disk
            EnvironmentCubemap, // 20-40%: Generating environment cubemap
            Irradiance,         // 40-55%: Generating irradiance map
            BRDF,               // 55-65%: Generating BRDF LUT
            Prefiltered,        // 65-95%: Generating prefiltered environment (10 mip levels)
            Skybox,             // 95-100%: Initializing skybox renderer
            Done                // Complete
        };

        Stage currentStage = Stage::HDRFile;
        int prefilteredMipLevel = 0;  // For tracking progress through 10 mip levels (0-9)

        static float getStageProgress(Stage stage, int mipLevel = 0)
        {
            switch (stage)
            {
                case Stage::HDRFile: return 0.0f;
                case Stage::EnvironmentCubemap: return 0.20f;
                case Stage::Irradiance: return 0.40f;
                case Stage::BRDF: return 0.55f;
                case Stage::Prefiltered: return 0.65f + (0.30f * mipLevel / 10.0f);
                case Stage::Skybox: return 0.95f;
                case Stage::Done: return 1.0f;
                default: return 0.0f;
            }
        }

        static const char* getStageName(Stage stage)
        {
            switch (stage)
            {
                case Stage::HDRFile: return "Loading HDR texture...";
                case Stage::EnvironmentCubemap: return "Generating environment cubemap...";
                case Stage::Irradiance: return "Generating irradiance map...";
                case Stage::BRDF: return "Generating BRDF LUT...";
                case Stage::Prefiltered: return "Generating prefiltered environment...";
                case Stage::Skybox: return "Initializing skybox...";
                case Stage::Done: return "Complete";
                default: return "Unknown";
            }
        }
    };
}
