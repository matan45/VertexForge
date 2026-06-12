#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace render
{
    // Per-frame GPU render-graph pass timings published by the graphics
    // module (RenderPassHandler drives RenderGraphProfiler's readback) and
    // read by the editor profiler UI. Same sink pattern as FrameDrawStats:
    // utilities owns the type so editor windows never include graphics
    // headers. GPU timestamps cost a little per pass, so collection is
    // request-driven and off by default — the editor flips the request,
    // graphics polls it once per frame.
    struct GpuPassTiming
    {
        std::string name;
        float ms = 0.0f;     // last completed frame
        float emaMs = 0.0f;  // smoothed (alpha 0.1)
    };

    struct GpuFrameStats
    {
        std::vector<GpuPassTiming> passTimings;
        float totalMs = 0.0f;
        float emaTotalMs = 0.0f;
        uint32_t barrierCount = 0;
        uint32_t barrierFlushCount = 0;
        bool valid = false;
    };

    class GpuPassStats
    {
    public:
        static constexpr size_t kHistorySize = 120;

        static GpuPassStats& instance()
        {
            static GpuPassStats inst;
            return inst;
        }

        void requestEnabled(bool e) { enabledRequest.store(e, std::memory_order_relaxed); }
        bool isEnabledRequested() const { return enabledRequest.load(std::memory_order_relaxed); }

        // Set once by graphics when query-pool init fails (no timestamp support)
        void markUnsupported() { unsupported.store(true, std::memory_order_relaxed); }
        bool isUnsupported() const { return unsupported.load(std::memory_order_relaxed); }

        void publish(GpuFrameStats stats)
        {
            std::lock_guard lock(mutex);
            if (stats.valid)
            {
                history[historyNext] = stats.totalMs;
                historyNext = (historyNext + 1) % kHistorySize;
                if (historyCount < kHistorySize) ++historyCount;
            }
            latest = std::move(stats);
        }

        GpuFrameStats snapshot() const
        {
            std::lock_guard lock(mutex);
            return latest;
        }

        // Frame totals oldest-to-newest, for history plots
        std::vector<float> totalMsHistory() const
        {
            std::lock_guard lock(mutex);
            std::vector<float> out;
            out.reserve(historyCount);
            size_t start = (historyNext + kHistorySize - historyCount) % kHistorySize;
            for (size_t i = 0; i < historyCount; ++i)
            {
                out.push_back(history[(start + i) % kHistorySize]);
            }
            return out;
        }

        void clear()
        {
            std::lock_guard lock(mutex);
            latest = {};
            historyNext = 0;
            historyCount = 0;
        }

    private:
        GpuPassStats() = default;

        mutable std::mutex mutex;
        GpuFrameStats latest;
        std::array<float, kHistorySize> history{};
        size_t historyNext = 0;
        size_t historyCount = 0;
        std::atomic<bool> enabledRequest{false};
        std::atomic<bool> unsupported{false};
    };
}
