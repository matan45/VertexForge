#include "HLODWorldBaker.hpp"

#include "world/HLODGenerator.hpp"
#include "print/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace services
{
    HLODWorldBaker::~HLODWorldBaker()
    {
        // A worker lambda captures `this` through the completed queue, so no job may outlive the
        // baker. Same discipline as MaterialPipelineWarmup::stop().
        if (cancellation)
            cancellation->cancel();
        waitForInFlight();

        // Anything that finished but was never drained left a temp file behind.
        std::lock_guard<std::mutex> lock(completedMutex);
        for (const auto& done : completed)
        {
            if (done.tempPath.empty())
                continue;

            std::error_code ec;
            std::filesystem::remove(done.tempPath, ec);
        }
        completed.clear();
    }

    std::string HLODWorldBaker::tempPathFor(const std::string& outputPath)
    {
        return outputPath + ".tmp";
    }

    size_t HLODWorldBaker::concurrencyLimit() const
    {
        return currentTier == 0 ? kMaxConcurrentTier0 : kMaxConcurrentCoarse;
    }

    bool HLODWorldBaker::begin(BakeRequest newRequest)
    {
        if (running)
            return false;
        if (newRequest.jobs.empty())
            return false;

        request = std::move(newRequest);
        cursor = 0;
        doneCount = 0;
        failedCount = 0;
        totalCount = static_cast<uint32_t>(request.jobs.size());
        currentTier = request.jobs.front().cell.tier;
        cancelled = false;
        running = true;
        cancellation = threading::CancellationToken::create();

        inFlight.clear();
        {
            std::lock_guard<std::mutex> lock(completedMutex);
            completed.clear();
        }

        vfLogInfo("HLOD bake started: {} cells", totalCount);

        submitReadyJobs();
        return true;
    }

    void HLODWorldBaker::cancel()
    {
        if (!running)
            return;

        cancelled = true;
        if (cancellation)
            cancellation->cancel();

        // Stop feeding the queue; jobs already dispatched run to completion (enkiTS has no
        // preemption) and their results are discarded in drainCompleted().
        cursor = request.jobs.size();

        vfLogInfo("HLOD bake cancelled after {}/{} cells", doneCount, totalCount);
    }

    void HLODWorldBaker::submitReadyJobs()
    {
        if (cancelled)
            return;

        // Drop handles for jobs that have already finished so inFlight reflects live work only.
        std::erase_if(inFlight, [](const threading::JobHandle& h) { return h.isComplete(); });

        while (cursor < request.jobs.size() && inFlight.size() < concurrencyLimit())
        {
            const CellJob& job = request.jobs[cursor];

            // The jobs list is ordered by tier, so a tier change here also narrows the
            // concurrency limit for everything that follows.
            if (job.cell.tier != currentTier)
            {
                // Let the finer tier drain before widening/narrowing, so peak RAM never mixes a
                // batch of 1-sector cells with a batch of 16-sector ones.
                if (!inFlight.empty())
                    return;
                currentTier = job.cell.tier;
            }

            ++cursor;

            // Everything the worker touches is copied into the lambda. `this` is captured only for
            // the completed queue, which the destructor drains after waiting on every handle.
            auto handle = threading::JobSystem::instance().submitJob(
                [this,
                 cell = job.cell,
                 tierConfig = job.tierConfig,
                 firstMember = job.firstMemberCoord,
                 memberFiles = job.memberSectorFiles,
                 outputPath = job.outputPath,
                 workingDirectory = request.workingDirectory,
                 sectorConfig = request.sectorConfig,
                 token = cancellation]()
                {
                    CompletedBake result;
                    result.cell = cell;
                    result.outputPath = outputPath;
                    result.tempPath = tempPathFor(outputPath);

                    if (!memberFiles.empty() && (!token || !token->isCancelled()))
                    {
                        // No progress callback: HLODGenerator's default reporter calls vfLog*,
                        // which races on global console state across workers.
                        ::world::HLODGenerator generator;
                        if (cell.tier == 0)
                        {
                            result.success = generator.generateForSector(
                                firstMember, memberFiles.front(), workingDirectory,
                                tierConfig, result.tempPath, nullptr);
                        }
                        else
                        {
                            result.success = generator.generateForCell(
                                cell, memberFiles, workingDirectory, tierConfig,
                                sectorConfig, result.tempPath, nullptr);
                        }
                    }

                    std::lock_guard<std::mutex> lock(completedMutex);
                    completed.push_back(std::move(result));
                },
                cancellation,
                threading::JobPriority::LOW);

            inFlight.push_back(std::move(handle));
        }
    }

    void HLODWorldBaker::drainCompleted()
    {
        std::vector<CompletedBake> batch;
        {
            std::lock_guard<std::mutex> lock(completedMutex);
            batch.swap(completed);
        }

        for (auto& done : batch)
        {
            std::error_code ec;

            if (cancelled || !done.success)
            {
                // Nothing is published on failure or cancellation - the previous bake, if any,
                // stays valid because the generator only ever wrote the temp file.
                std::filesystem::remove(done.tempPath, ec);

                if (!cancelled)
                {
                    ++failedCount;
                    ++doneCount;
                    vfLogWarning("HLOD bake failed for cell [{},{},T{}]",
                                 done.cell.x, done.cell.z, done.cell.tier);
                }
                continue;
            }

            // Rename last so the .vfHLOD only ever appears complete. std::filesystem::rename
            // refuses to clobber on some platforms, so drop any previous bake first.
            std::filesystem::remove(done.outputPath, ec);
            std::filesystem::rename(done.tempPath, done.outputPath, ec);

            if (ec)
            {
                ++failedCount;
                ++doneCount;
                vfLogWarning("HLOD bake could not publish [{},{},T{}]: {}",
                             done.cell.x, done.cell.z, done.cell.tier, ec.message());
                std::error_code cleanup;
                std::filesystem::remove(done.tempPath, cleanup);
                continue;
            }

            ++doneCount;
            if (onCellBaked)
                onCellBaked(done.cell, done.outputPath);
        }
    }

    void HLODWorldBaker::update()
    {
        if (!running)
            return;

        drainCompleted();
        submitReadyJobs();

        std::erase_if(inFlight, [](const threading::JobHandle& h) { return h.isComplete(); });

        const bool queueDrained = cursor >= request.jobs.size();
        if (queueDrained && inFlight.empty())
        {
            // One last drain: a job may have finished between drainCompleted() and the handle
            // sweep above, leaving its result unpublished.
            drainCompleted();
            finish();
        }
    }

    void HLODWorldBaker::finish()
    {
        if (cancelled)
            vfLogInfo("HLOD bake stopped ({} of {} cells completed)", doneCount, totalCount);
        else
            vfLogInfo("HLOD bake complete: {} cells, {} failed", doneCount, failedCount);

        running = false;
        request.jobs.clear();
        inFlight.clear();
        cancellation.reset();
    }

    void HLODWorldBaker::waitForInFlight()
    {
        for (const auto& handle : inFlight)
            handle.wait();
        inFlight.clear();
    }

    ::events::world::hlod::HLODBakeProgress HLODWorldBaker::getProgress() const
    {
        ::events::world::hlod::HLODBakeProgress progress;
        progress.running = running;
        progress.cancelled = cancelled;
        progress.cellsDone = doneCount;
        progress.cellsTotal = totalCount;
        progress.cellsFailed = failedCount;
        progress.currentTier = currentTier;
        return progress;
    }
}
