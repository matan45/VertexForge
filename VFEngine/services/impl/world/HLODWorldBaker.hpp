#pragma once

#include "../../events/world/HLODEvents.hpp"
#include "world/HLODTypes.hpp"
#include "world/WorldTypes.hpp"
#include "threading/JobSystem.hpp"
#include "threading/CancellationToken.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace services
{
    // VK-1594: whole-world, all-tier HLOD bake driven off the JobSystem.
    //
    // HLODGenerator is pure CPU - sector JSON parse, mesh LOD0 reads, meshoptimizer, file write -
    // and every VirtualFileSystem / SerializationFileAccess read it performs is behind a
    // shared_mutex, so the generation itself is safe on a worker thread. Two things are not:
    //
    //   * vfLog* mutates global console colour state with no lock (Log.hpp), so workers stay
    //     silent and every log line is emitted from the main thread on drain;
    //   * recording a result touches sectorManager / worldDefinition, which belong to the
    //     main-thread-pinned "WorldSector" frame task (EditorFrameTaskGraph, VK-1588).
    //
    // So workers only produce a temp file and push a result onto a mutex-guarded queue; update()
    // drains it on the main thread, renames the temp into place, and calls onCellBaked.
    //
    // The work list is an immutable snapshot taken at begin(). Save World is refused while a bake
    // is running (WorldSectorServiceImpl), so the .vfsector files the workers read cannot be
    // rewritten underneath them.
    class HLODWorldBaker
    {
    public:
        struct CellJob
        {
            ::world::HLODCellCoord cell;
            ::world::HLODTierConfig tierConfig;
            ::world::SectorCoord firstMemberCoord;
            std::vector<std::string> memberSectorFiles;
            std::string outputPath;
        };

        struct BakeRequest
        {
            // Ordered fine tier first, so the near ring becomes correct before the far one.
            std::vector<CellJob> jobs;
            std::string workingDirectory;
            ::world::SectorConfig sectorConfig;
        };

        HLODWorldBaker() = default;
        ~HLODWorldBaker();

        HLODWorldBaker(const HLODWorldBaker&) = delete;
        HLODWorldBaker& operator=(const HLODWorldBaker&) = delete;

        // Rejects the request while another bake is running. Returns false for an empty work list.
        bool begin(BakeRequest request);

        // Cooperative: jobs already running finish (enkiTS cannot preempt), their output is
        // discarded, and their temp files are removed.
        void cancel();

        // Pumped once per frame from WorldSectorServiceImpl::update() on the main thread.
        void update();

        [[nodiscard]] bool isRunning() const { return running; }
        [[nodiscard]] ::events::world::hlod::HLODBakeProgress getProgress() const;

        // Called on the MAIN thread for each successful bake, after the temp file is in place.
        void setCellBakedCallback(
            std::function<void(const ::world::HLODCellCoord&, const std::string&)> callback)
        {
            onCellBaked = std::move(callback);
        }

    private:
        // A tier-2 cell merges 16 sectors' worth of source meshes in RAM at once, so the coarse
        // tiers run narrower than tier 0. Concurrency is capped by cell footprint, not core count.
        static constexpr size_t kMaxConcurrentTier0 = 4;
        static constexpr size_t kMaxConcurrentCoarse = 2;

        struct CompletedBake
        {
            ::world::HLODCellCoord cell;
            std::string outputPath;
            std::string tempPath;
            bool success = false;
        };

        BakeRequest request;
        size_t cursor = 0;
        bool running = false;
        bool cancelled = false;

        uint32_t totalCount = 0;
        uint32_t doneCount = 0;
        uint32_t failedCount = 0;
        uint8_t currentTier = 0;

        std::vector<threading::JobHandle> inFlight;
        threading::CancellationToken::Ptr cancellation;

        mutable std::mutex completedMutex;
        std::vector<CompletedBake> completed;

        std::function<void(const ::world::HLODCellCoord&, const std::string&)> onCellBaked;

        void submitReadyJobs();
        void drainCompleted();
        void waitForInFlight();
        void finish();

        [[nodiscard]] size_t concurrencyLimit() const;
        static std::string tempPathFor(const std::string& outputPath);
    };
}
