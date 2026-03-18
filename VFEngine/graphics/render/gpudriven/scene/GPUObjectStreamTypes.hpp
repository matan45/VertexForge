#pragma once

#include <cstdint>

namespace render::gpudriven
{
    enum class ObjectStreamState : uint8_t
    {
        NotLoaded,
        Queued,
        Active
    };

    struct ObjectStreamConfig
    {
        uint32_t maxUploadsPerFrame = 64;
        uint32_t maxEvictionsPerFrame = 32;
        float evictionThreshold = 0.9f;
        float evictionTarget = 0.8f;
        float hysteresisMargin = 0.05f;
    };

    struct ObjectStreamingStats
    {
        uint32_t totalRegistered = 0;
        uint32_t activeOnGPU = 0;
        uint32_t queuedForUpload = 0;
        uint32_t uploadsThisFrame = 0;
        uint32_t evictionsThisFrame = 0;
        float slotUtilization = 0.0f;
        float fragmentation = 0.0f;
    };
}
