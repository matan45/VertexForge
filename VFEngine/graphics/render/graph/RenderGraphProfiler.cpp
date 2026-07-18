#include "RenderGraphProfiler.hpp"
#include "../../core/Device.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace render::graph
{
    bool RenderGraphProfiler::init(core::Device& device, uint32_t maxPassCount)
    {
        maxPasses = maxPassCount;
        // Boundary chain: N passes need N+1 boundaries — one opened at
        // beginFrame, then one closed per endPass.
        return queryPool.init(device, maxPassCount + 1);
    }

    void RenderGraphProfiler::cleanup(const vk::Device& logicalDevice)
    {
        queryPool.cleanup(logicalDevice);
    }

    void RenderGraphProfiler::setEnabled(bool e)
    {
        if (enabled == e) return;
        enabled = e;
        if (e) return;

        // Nothing resets the pools while disabled, so their queries stay
        // AVAILABLE. Dropping the slot bookkeeping stops the next enable from
        // reading arbitrarily old timestamps and publishing one bogus spike.
        for (auto& ready : frameSlotReady) ready = false;
        for (auto& count : slotPassCount) count = 0;
        for (auto& names : slotPassNames) names.clear();
        lastStats = {};
        passEma.clear();
        emaTotalMs = 0.0f;
        totalEmaSeeded = false;
    }

    void RenderGraphProfiler::beginFrame(vk::CommandBuffer cmd, uint32_t imageIndex)
    {
        if (!enabled || !queryPool.isValid()) return;

        queryPool.resetFrame(cmd, imageIndex);

        // B[0] — the chain's opening boundary.
        queryPool.writeTimestamp(cmd, imageIndex, 0, vk::PipelineStageFlagBits::eBottomOfPipe);

        uint32_t fi = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
        frameSlotReady[fi] = true;
        currentPassCount = 0;
        slotPassNames[fi].clear();
    }

    void RenderGraphProfiler::beginPass(vk::CommandBuffer cmd, uint32_t imageIndex,
                                         uint32_t sortedPassIndex, const std::string& passName)
    {
        if (!enabled || !queryPool.isValid()) return;
        if (sortedPassIndex >= maxPasses) return;

        // No timestamp here — this pass opens on the previous pass's closing
        // boundary (B[0] for the first). RenderGraph flushes the pass's barriers
        // immediately before this call, so their cost now lands inside the pass
        // that required them instead of in a gap nobody attributes.
        slotPassNames[imageIndex % core::MAX_FRAMES_IN_FLIGHT].push_back(passName);
        currentPassCount = sortedPassIndex + 1;
    }

    void RenderGraphProfiler::endPass(vk::CommandBuffer cmd, uint32_t imageIndex,
                                       uint32_t sortedPassIndex)
    {
        if (!enabled || !queryPool.isValid()) return;
        if (sortedPassIndex >= maxPasses) return;

        // B[i+1] — closes pass i and opens pass i+1.
        queryPool.writeTimestamp(cmd, imageIndex, sortedPassIndex + 1,
                                 vk::PipelineStageFlagBits::eBottomOfPipe);
    }

    void RenderGraphProfiler::endFrame(vk::CommandBuffer cmd, uint32_t imageIndex)
    {
        if (!enabled || !queryPool.isValid()) return;

        // Record how many passes were profiled into this slot so the readback
        // reads exactly the written, dense boundary range.
        slotPassCount[imageIndex % core::MAX_FRAMES_IN_FLIGHT] = currentPassCount;
    }

    bool RenderGraphProfiler::readbackAndUpdate(const vk::Device& logicalDevice, uint32_t imageIndex)
    {
        if (!queryPool.isValid()) return false;

        // Read THIS slot, not (imageIndex - 1)'s. imageIndex is the acquired
        // swapchain image index: the swapchain holds minImageCount+1 images and
        // acquireNextImageKHR promises no ordering at all, so it does not step by
        // one and the previous frame's slot cannot be inferred from it -- doing so
        // re-reads one slot while another is reset unread. OffScreenViewPort waits
        // inFlightFences[imageIndex % MAX_FRAMES_IN_FLIGHT] before recording and
        // the pool keys off that same modulus, so this slot's previous occupant is
        // fence-guaranteed complete; beginFrame has not reset it yet at this point.
        // Every write is therefore read exactly once, whatever the image count.
        const uint32_t fi = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
        if (!frameSlotReady[fi]) return false;

        const uint32_t n = slotPassCount[fi];
        if (n == 0) return false;

        std::vector<uint64_t> boundaries;
        if (!queryPool.readResults(logicalDevice, imageIndex, boundaries, n + 1)) return false;
        if (boundaries.size() < static_cast<size_t>(n) + 1) return false;

        ++readbackFrame;

        std::vector<float> passMs;
        float totalMs = 0.0f;
        timing::passDeltasFromBoundaries(boundaries, queryPool.getTimestampPeriod(),
                                         queryPool.getTimestampValidBits(), passMs, totalMs);

        emaTotalMs = totalEmaSeeded
            ? (timing::EMA_ALPHA * totalMs + (1.0f - timing::EMA_ALPHA) * emaTotalMs)
            : totalMs;
        totalEmaSeeded = true;

        // Names come from the same slot as the timestamps, so they always
        // describe the frame that produced them.
        const auto& names = slotPassNames[fi];

        RenderGraphStats stats;
        stats.passCount = n;
        stats.totalMs = totalMs;
        stats.emaTotalMs = emaTotalMs;
        stats.barrierCount = barrierCount;
        stats.barrierFlushCount = barrierFlushCount;
        stats.passTimings.reserve(std::min(names.size(), passMs.size()));
        for (size_t i = 0; i < names.size() && i < passMs.size(); ++i)
        {
            stats.passTimings.push_back({names[i], passMs[i],
                                         passEma.update(names[i], passMs[i], readbackFrame)});
        }
        passEma.prune(readbackFrame, kEmaMaxAgeFrames);

        lastStats = std::move(stats);
        return true;
    }
}
