#pragma once

#include "../raytracing/GPUTimestampQueryPool.hpp"
#include "../../core/GraphicsConstants.hpp"
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

    class RenderGraphProfiler
    {
    public:
        RenderGraphProfiler() = default;
        ~RenderGraphProfiler() = default;

        RenderGraphProfiler(const RenderGraphProfiler&) = delete;
        RenderGraphProfiler& operator=(const RenderGraphProfiler&) = delete;

        bool init(core::Device& device, uint32_t maxPasses);
        void cleanup(const vk::Device& logicalDevice);

        void beginFrame(vk::CommandBuffer cmd, uint32_t frameIndex);
        void beginPass(vk::CommandBuffer cmd, uint32_t frameIndex,
                       uint32_t sortedPassIndex, const std::string& passName);
        void endPass(vk::CommandBuffer cmd, uint32_t frameIndex,
                     uint32_t sortedPassIndex);
        void endFrame(vk::CommandBuffer cmd, uint32_t frameIndex);

        // Read results from previous frame and update EMA
        void readbackAndUpdate(const vk::Device& logicalDevice, uint32_t frameIndex);

        RenderGraphStats getStats() const;

        bool isEnabled() const { return enabled; }
        void setEnabled(bool e) { enabled = e; }

        void setBarrierStats(uint32_t count, uint32_t flushes)
        {
            barrierCount = count;
            barrierFlushCount = flushes;
        }

    private:
        void updateEMA(float sample, float& ema) const;

        raytracing::GPUTimestampQueryPool queryPool;

        // Per-pass EMA state + last raw samples (raw totals feed the
        // profiler UI's history plot, where EMA would hide hitches)
        std::vector<std::string> passNames;
        std::vector<float> emaTimes;
        std::vector<float> lastTimes;
        float emaTotalMs = 0.0f;
        float lastTotalMs = 0.0f;
        bool emaInitialized = false;

        uint32_t maxPasses = 0;
        uint32_t currentPassCount = 0;

        uint32_t barrierCount = 0;
        uint32_t barrierFlushCount = 0;

        bool enabled = false;

        // Track which frame slots have been reset
        bool frameSlotReady[core::MAX_FRAMES_IN_FLIGHT]{};

        static constexpr float EMA_ALPHA = 0.1f;
    };
}
