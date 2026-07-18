#pragma once

#include "../raytracing/GPUTimestampQueryPool.hpp"
#include "../../core/GraphicsConstants.hpp"
#include "stats/GpuTimingMath.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <string>

namespace core
{
    class Device;
}

namespace render::graph
{
    struct PassTiming
    {
        std::string name;
        float ms = 0.0f;
        float emaMs = 0.0f;
    };

    struct RenderGraphStats
    {
        std::vector<PassTiming> passTimings;
        float totalMs = 0.0f;
        float emaTotalMs = 0.0f;
        uint32_t passCount = 0;
        uint32_t barrierCount = 0;
        uint32_t barrierFlushCount = 0;
    };

    /// Per-pass GPU timings for the render graph, as a boundary chain: N+1
    /// bottom-of-pipe timestamps bracket N passes, so pass i spans B[i]..B[i+1]
    /// and the frame total is B[0]..B[N] by construction. Paired
    /// top-of-pipe/bottom-of-pipe timestamps would overlap between adjacent
    /// passes -- each pass re-counting its predecessors' drain -- so summing them
    /// overshoots the real frame time, by a driver-dependent margin (VK-1529).
    class RenderGraphProfiler
    {
    public:
        RenderGraphProfiler() = default;
        ~RenderGraphProfiler() = default;

        RenderGraphProfiler(const RenderGraphProfiler&) = delete;
        RenderGraphProfiler& operator=(const RenderGraphProfiler&) = delete;

        bool init(core::Device& device, uint32_t maxPasses);
        void cleanup(const vk::Device& logicalDevice);

        void beginFrame(vk::CommandBuffer cmd, uint32_t imageIndex);
        void beginPass(vk::CommandBuffer cmd, uint32_t imageIndex,
                       uint32_t sortedPassIndex, const std::string& passName);
        void endPass(vk::CommandBuffer cmd, uint32_t imageIndex,
                     uint32_t sortedPassIndex);
        void endFrame(vk::CommandBuffer cmd, uint32_t imageIndex);

        /// Read this slot's completed results and rebuild the published stats.
        /// Returns false when no fresh sample was produced, so the caller can
        /// avoid republishing the previous frame's numbers as if they were new.
        bool readbackAndUpdate(const vk::Device& logicalDevice, uint32_t imageIndex);

        const RenderGraphStats& getStats() const { return lastStats; }

        bool isEnabled() const { return enabled; }
        void setEnabled(bool e);

        void setBarrierStats(uint32_t count, uint32_t flushes)
        {
            barrierCount = count;
            barrierFlushCount = flushes;
        }

    private:
        raytracing::GPUTimestampQueryPool queryPool;

        // Rebuilt wholesale by readbackAndUpdate from one slot's names + times,
        // so what getStats() returns can never mix two frames.
        RenderGraphStats lastStats;

        // Keyed by pass name, not index: graph passes are conditional, and a
        // positional key blends one pass's history into another's the moment any
        // pass toggles.
        timing::NamedEma passEma;
        float emaTotalMs = 0.0f;
        bool totalEmaSeeded = false;

        // Monotonic readback counter, only used to age out NamedEma entries.
        uint64_t readbackFrame = 0;

        uint32_t maxPasses = 0;
        uint32_t currentPassCount = 0;

        uint32_t barrierCount = 0;
        uint32_t barrierFlushCount = 0;

        bool enabled = false;

        // Track which frame slots have been reset
        bool frameSlotReady[core::MAX_FRAMES_IN_FLIGHT]{};

        // Number of passes profiled into each frame slot (set at endFrame). The
        // readback reads exactly passCount+1 boundaries — the dense written range
        // — so unwritten tail queries never force a VK_NOT_READY.
        uint32_t slotPassCount[core::MAX_FRAMES_IN_FLIGHT]{};

        // Pass names per slot, so a readback pairs the times it just read with
        // the names recorded into that same slot. A single shared vector would
        // silently mis-attribute names whenever the slot read is not the slot
        // most recently recorded.
        std::vector<std::string> slotPassNames[core::MAX_FRAMES_IN_FLIGHT];

        // Forget a pass's EMA after it has been absent this long.
        static constexpr uint64_t kEmaMaxAgeFrames = 600;
    };
}
