#pragma once
#include "data/HeightmapGenerationData.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>

namespace handlers
{
    // VK-1653: serves the heightmap generation events (Begin, Poll) behind MCP
    // terrain_generate_heightmap. Lives in Editor.exe because ProceduralGen.dll is linked by the
    // Editor only and EventDispatcher is per-binary -- the same shape as ExportHandler.
    //
    // One job at a time, on a std::async worker that touches only ProceduralGen, the file system
    // and the log. Validation, the .vfmeta GUID lookup and the AssetSavedNotification happen on the
    // main thread, inside Begin and Poll.
    class HeightmapGenerationHandler
    {
    private:
        struct FinishedJob
        {
            uint64_t jobId = 0;
            services::HeightmapJobStatus status;
        };

        // Finished results Poll can still report; the oldest is evicted first.
        static constexpr size_t maxFinishedJobs = 8;

        uint64_t nextJobId = 1;
        uint64_t runningJobId = 0;                  // 0 = idle
        services::HeightmapJobStatus runningStatus; // what Poll reports while the job runs
        std::atomic<float> progress{0.0f};          // written by the worker
        std::future<services::HeightmapJobStatus> runningJob;
        std::deque<FinishedJob> finishedJobs;

    public:
        HeightmapGenerationHandler() = default;
        ~HeightmapGenerationHandler();

        void registerEventHandlers();
        void unregisterEventHandlers();

    private:
        services::HeightmapJobStart handleBegin(const services::HeightmapGenerationRequest& request);
        services::HeightmapJobStatus handlePoll(uint64_t jobId);

        // Moves a completed job into finishedJobs and publishes its AssetSavedNotification --
        // exactly once, on whichever main-thread call (Poll, or the next Begin) sees it first.
        void collectFinishedJob();
    };
}
