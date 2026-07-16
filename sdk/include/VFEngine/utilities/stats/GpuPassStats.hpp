#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace render
{
    // GPU timings published by the graphics module (RenderPassHandler drives the
    // readback) and read by the editor profiler UI. Same sink pattern as
    // FrameDrawStats: utilities owns the type so editor windows never include
    // graphics headers.
    //
    // Two tiers, because the status bar and the profiler window want very
    // different things (VK-1529):
    //
    //   Tier 1 - whole-frame GPU span. Two timestamps per frame, always on, held
    //            in atomics because the status bar reads it every frame and must
    //            not contend with the render thread. This is the honest frame
    //            number: it brackets the entire offscreen command buffer.
    //   Tier 2 - per-pass timings. One timestamp per pass boundary, so it is
    //            request-driven and off by default -- the editor flips the
    //            request, graphics polls it once per frame.
    //
    // Both tiers cover the graphics queue only; async-compute work runs on its
    // own queue and is not timestamped.
    struct GpuPassTiming
    {
        std::string name;
        float ms = 0.0f;     // last completed frame
        float emaMs = 0.0f;  // smoothed (alpha 0.1)
        // Aux scopes (VT/terrain, VK-1480) are sub-intervals nested INSIDE a
        // graph pass, not siblings of it. Their cost is already counted in
        // totalMs, so they must be kept out of any share-of-frame denominator.
        bool isAux = false;
    };

    struct GpuFrameStats
    {
        std::vector<GpuPassTiming> passTimings;
        float totalMs = 0.0f;     // first-to-last pass boundary (graph only)
        float emaTotalMs = 0.0f;
        uint32_t barrierCount = 0;
        uint32_t barrierFlushCount = 0;
        // Readback is non-blocking, so a frame whose results are not ready yet
        // contributes no sample. Counting the misses keeps a dropped sample
        // distinguishable from a real one instead of silently republishing the
        // previous frame's numbers.
        uint32_t droppedSamples = 0;
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

        // ---- Tier 1: whole-frame GPU span (always collected) ----

        // Lock-free on purpose: the status bar reads these every frame.
        void publishFrameTime(float ms, float emaMs)
        {
            frameMsValue.store(ms, std::memory_order_relaxed);
            emaFrameMsValue.store(emaMs, std::memory_order_relaxed);
            frameValid.store(true, std::memory_order_release);

            std::lock_guard lock(mutex);
            history[historyNext] = ms;
            historyNext = (historyNext + 1) % kHistorySize;
            if (historyCount < kHistorySize) ++historyCount;
        }

        bool hasFrameGpuTime() const { return frameValid.load(std::memory_order_acquire); }
        float frameGpuMs() const { return frameMsValue.load(std::memory_order_relaxed); }
        float emaFrameGpuMs() const { return emaFrameMsValue.load(std::memory_order_relaxed); }

        // Whole-frame GPU ms oldest-to-newest, for history plots. Fed by tier 1,
        // so the plot is already populated when the profiler window opens.
        std::vector<float> frameMsHistory() const
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

        // ---- Tier 2: per-pass timings (request-driven) ----

        void requestEnabled(bool e) { enabledRequest.store(e, std::memory_order_relaxed); }
        bool isEnabledRequested() const { return enabledRequest.load(std::memory_order_relaxed); }

        void publish(GpuFrameStats stats)
        {
            std::lock_guard lock(mutex);
            latest = std::move(stats);
        }

        GpuFrameStats snapshot() const
        {
            std::lock_guard lock(mutex);
            return latest;
        }

        // Drops the per-pass timings only. The frame-time history belongs to
        // tier 1, which keeps running, so it deliberately survives.
        void clear()
        {
            std::lock_guard lock(mutex);
            latest = {};
        }

        // ---- Shared ----

        // Set once by graphics when query-pool init fails (no timestamp support)
        void markUnsupported() { unsupported.store(true, std::memory_order_relaxed); }
        bool isUnsupported() const { return unsupported.load(std::memory_order_relaxed); }

    private:
        GpuPassStats() = default;

        mutable std::mutex mutex;
        GpuFrameStats latest;
        std::array<float, kHistorySize> history{};
        size_t historyNext = 0;
        size_t historyCount = 0;

        std::atomic<float> frameMsValue{0.0f};
        std::atomic<float> emaFrameMsValue{0.0f};
        std::atomic<bool> frameValid{false};

        std::atomic<bool> enabledRequest{false};
        std::atomic<bool> unsupported{false};
    };
}
