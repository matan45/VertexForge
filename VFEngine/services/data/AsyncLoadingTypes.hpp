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
}
