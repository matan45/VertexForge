#include "RenderGraphProfiler.hpp"
#include "../../core/Device.hpp"
#include "print/Log.hpp"

namespace render::graph
{
    bool RenderGraphProfiler::init(core::Device& device, uint32_t maxPassCount)
    {
        maxPasses = maxPassCount;
        // 2 queries per pass (start + end)
        return queryPool.init(device, maxPassCount * 2);
    }

    void RenderGraphProfiler::cleanup(const vk::Device& logicalDevice)
    {
        queryPool.cleanup(logicalDevice);
    }

    void RenderGraphProfiler::beginFrame(vk::CommandBuffer cmd, uint32_t frameIndex)
    {
        if (!enabled || !queryPool.isValid()) return;

        queryPool.resetFrame(cmd, frameIndex);
        uint32_t fi = frameIndex % core::MAX_FRAMES_IN_FLIGHT;
        frameSlotReady[fi] = true;
        currentPassCount = 0;
        passNames.clear();
    }

    void RenderGraphProfiler::beginPass(vk::CommandBuffer cmd, uint32_t frameIndex,
                                         uint32_t sortedPassIndex, const std::string& passName)
    {
        if (!enabled || !queryPool.isValid()) return;
        if (sortedPassIndex >= maxPasses) return;

        passNames.push_back(passName);
        currentPassCount = sortedPassIndex + 1;

        // Write start timestamp (query index = sortedPassIndex * 2)
        queryPool.writeTimestamp(cmd, frameIndex, sortedPassIndex * 2,
                                vk::PipelineStageFlagBits::eTopOfPipe);
    }

    void RenderGraphProfiler::endPass(vk::CommandBuffer cmd, uint32_t frameIndex,
                                       uint32_t sortedPassIndex)
    {
        if (!enabled || !queryPool.isValid()) return;
        if (sortedPassIndex >= maxPasses) return;

        // Write end timestamp (query index = sortedPassIndex * 2 + 1)
        queryPool.writeTimestamp(cmd, frameIndex, sortedPassIndex * 2 + 1,
                                vk::PipelineStageFlagBits::eBottomOfPipe);
    }

    void RenderGraphProfiler::endFrame(vk::CommandBuffer cmd, uint32_t frameIndex)
    {
        if (!enabled || !queryPool.isValid()) return;

        // Record how many passes were profiled into this slot so the readback (a
        // frame or two later) reads exactly the written, dense query range.
        slotPassCount[frameIndex % core::MAX_FRAMES_IN_FLIGHT] = currentPassCount;
    }

    void RenderGraphProfiler::readbackAndUpdate(const vk::Device& logicalDevice, uint32_t frameIndex)
    {
        if (!queryPool.isValid()) return;

        uint32_t prevFI = (frameIndex + core::MAX_FRAMES_IN_FLIGHT - 1) % core::MAX_FRAMES_IN_FLIGHT;
        if (!frameSlotReady[prevFI]) return;

        // Read exactly the dense, written range for that slot (2 queries per pass).
        uint32_t n = slotPassCount[prevFI];
        if (n == 0) return;

        std::vector<uint64_t> timestamps;
        if (!queryPool.readResults(logicalDevice, prevFI, timestamps, n * 2)) return;

        float totalMs = 0.0f;

        // Resize EMA array if needed
        if (emaTimes.size() < n)
            emaTimes.resize(n, 0.0f);
        lastTimes.assign(n, 0.0f);

        for (uint32_t i = 0; i < n && (i * 2 + 1) < timestamps.size(); ++i)
        {
            float ms = queryPool.toMilliseconds(timestamps[i * 2], timestamps[i * 2 + 1]);
            updateEMA(ms, emaTimes[i]);
            lastTimes[i] = ms;
            totalMs += ms;
        }

        updateEMA(totalMs, emaTotalMs);
        lastTotalMs = totalMs;
        emaInitialized = true;
    }

    RenderGraphStats RenderGraphProfiler::getStats() const
    {
        RenderGraphStats stats{};
        stats.passCount = currentPassCount;
        stats.totalMs = lastTotalMs;
        stats.emaTotalMs = emaTotalMs;
        stats.barrierCount = barrierCount;
        stats.barrierFlushCount = barrierFlushCount;

        for (uint32_t i = 0; i < currentPassCount && i < passNames.size(); ++i)
        {
            PassTiming timing{};
            timing.name = passNames[i];
            timing.emaMs = (i < emaTimes.size()) ? emaTimes[i] : 0.0f;
            timing.ms = (i < lastTimes.size()) ? lastTimes[i] : timing.emaMs;
            stats.passTimings.push_back(timing);
        }

        return stats;
    }

    void RenderGraphProfiler::updateEMA(float sample, float& ema) const
    {
        if (!emaInitialized)
            ema = sample;
        else
            ema = EMA_ALPHA * sample + (1.0f - EMA_ALPHA) * ema;
    }
}
