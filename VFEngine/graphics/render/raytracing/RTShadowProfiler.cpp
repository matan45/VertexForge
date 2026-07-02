#include "RTShadowProfiler.hpp"
#include "RTShadowBudget.hpp"
#include "../../core/Device.hpp"
#include "../../../utilities/print/Log.hpp"
#include <algorithm>

namespace render::raytracing
{
    bool RTShadowProfiler::init(core::Device& device)
    {
        return queryPool.init(device, RTShadowTimestamp::Count);
    }

    void RTShadowProfiler::cleanup(const vk::Device& logicalDevice)
    {
        queryPool.cleanup(logicalDevice);
    }

    void RTShadowProfiler::resetFrame(vk::CommandBuffer cmd, uint32_t frameIndex)
    {
        blasBuiltThisFrame = false;
        tlasBuiltThisFrame = false;
        queryPool.resetFrame(cmd, frameIndex);
        uint32_t fi = frameIndex % core::MAX_FRAMES_IN_FLIGHT;
        frameSlotReady[fi] = true;
    }

    void RTShadowProfiler::writeTimestamp(vk::CommandBuffer cmd, uint32_t frameIndex,
                                           RTShadowTimestamp slot, vk::PipelineStageFlagBits stage)
    {
        queryPool.writeTimestamp(cmd, frameIndex, static_cast<uint32_t>(slot), stage);
    }

    void RTShadowProfiler::updateEMA(float sample, float& ema) const
    {
        if (!emaInitialized)
            ema = sample;
        else
            ema = EMA_ALPHA * sample + (1.0f - EMA_ALPHA) * ema;
    }

    void RTShadowProfiler::readbackAndUpdate(const vk::Device& logicalDevice, uint32_t frameIndex,
                                              const ASMemoryBudget& asBudget)
    {
        if (!queryPool.isValid()) return;

        // Read the PREVIOUS frame's slot (the one that has completed on the GPU).
        // Current frame's slot was just reset — it's not ready to read.
        uint32_t prevFI = (frameIndex + core::MAX_FRAMES_IN_FLIGHT - 1) % core::MAX_FRAMES_IN_FLIGHT;
        if (!frameSlotReady[prevFI]) return;

        // VK-1479 C5: read exactly the original six directional + AS-build slots (0..5). The
        // shared query pool now also holds the spot/point slots (6..11), which are unwritten
        // whenever spot/point RT is inactive (the default); capping the read here keeps those
        // unavailable queries from making this non-blocking read return VK_NOT_READY. This branch
        // is byte-identical to pre-C5 behavior.
        std::vector<uint64_t> timestamps;
        if (!queryPool.readResults(logicalDevice, prevFI, timestamps,
                                   RTShadowTimestamp::BeforeSpotDispatch))
            return;

        // Ray dispatch timing
        float rayMs = queryPool.toMilliseconds(
            timestamps[RTShadowTimestamp::BeforeRayDispatch],
            timestamps[RTShadowTimestamp::AfterRayDispatch]);

        // Denoiser timing (AfterRayDispatch -> AfterDenoiser)
        float denoiserMs = queryPool.toMilliseconds(
            timestamps[RTShadowTimestamp::AfterRayDispatch],
            timestamps[RTShadowTimestamp::AfterDenoiser]);

        updateEMA(rayMs, emaRayMs);
        updateEMA(denoiserMs, emaDenoiserMs);

        // BLAS/TLAS build timing (only when builds occurred)
        if (blasBuiltThisFrame)
        {
            float blasMs = queryPool.toMilliseconds(
                timestamps[RTShadowTimestamp::BeforeBLASBuild],
                timestamps[RTShadowTimestamp::AfterBLASBuild]);
            updateEMA(blasMs, emaBlasBuildMs);
        }
        if (tlasBuiltThisFrame)
        {
            float tlasMs = queryPool.toMilliseconds(
                timestamps[RTShadowTimestamp::AfterBLASBuild],
                timestamps[RTShadowTimestamp::AfterTLASBuild]);
            updateEMA(tlasMs, emaTlasBuildMs);
        }

        // VK-1479 C5: additively fold in the spot/point trace+denoise cost so the adaptive budget
        // sees the full RT-shadow bill. Only attempted when spot/point RT is active — the default
        // path does zero extra reads and totalMs stays directional-only (byte-identical). A slot's
        // availability implies its pass ran last frame (reset-but-unwritten queries stay
        // unavailable), so a successful prefix read is itself the gate on the EMA update.
        // Limitation: prefix-only reads can't isolate point from spot, so point cost is folded only
        // when spot RT also ran that frame (otherwise it is conservatively omitted — never
        // over-counted). See the note in dispatchRTPointShadow / the C5 handoff.
        float spotMs = 0.0f;
        float pointMs = 0.0f;
        if (spotActive || pointActive)
        {
            std::vector<uint64_t> ext;
            if (queryPool.readResults(logicalDevice, prevFI, ext, RTShadowTimestamp::Count))
            {
                // Slots 0..11 all available: both spot and point ran last frame.
                spotMs = queryPool.toMilliseconds(
                    ext[RTShadowTimestamp::BeforeSpotDispatch],
                    ext[RTShadowTimestamp::AfterSpotDenoiser]);
                pointMs = queryPool.toMilliseconds(
                    ext[RTShadowTimestamp::BeforePointDispatch],
                    ext[RTShadowTimestamp::AfterPointDenoiser]);
                updateEMA(spotMs, emaSpotMs);
                updateEMA(pointMs, emaPointMs);
            }
            else if (queryPool.readResults(logicalDevice, prevFI, ext,
                                           RTShadowTimestamp::BeforePointDispatch))
            {
                // Slots 0..8 available: spot ran, point did not (or its slots aren't ready).
                spotMs = queryPool.toMilliseconds(
                    ext[RTShadowTimestamp::BeforeSpotDispatch],
                    ext[RTShadowTimestamp::AfterSpotDenoiser]);
                updateEMA(spotMs, emaSpotMs);
            }
        }

        float totalMs = rayMs + denoiserMs + spotMs + pointMs;
        updateEMA(totalMs, emaTotalMs);

        emaInitialized = true;

        // AS memory budget check
        float usedBytes = static_cast<float>(asBudget.blasTotalBytes + asBudget.tlasTotalBytes);
        asOverBudget = usedBytes > asMemoryBudgetBytes;
        if (asOverBudget)
        {
            float usedMB = usedBytes / (1024.0f * 1024.0f);
            float budgetMB = asMemoryBudgetBytes / (1024.0f * 1024.0f);
            vfLogWarning("RT Shadow AS memory exceeds budget: {:.1f} MB / {:.1f} MB", usedMB, budgetMB);
        }

        // Update hysteresis counters
        if (adaptiveEnabled && emaTotalMs > budgetMs)
        {
            consecutiveOverBudget++;
            consecutiveUnderBudget = 0;
        }
        else if (adaptiveEnabled && emaTotalMs < budgetMs * RESTORE_THRESHOLD)
        {
            consecutiveUnderBudget++;
            consecutiveOverBudget = 0;
        }
        else
        {
            consecutiveOverBudget = 0;
            consecutiveUnderBudget = 0;
        }
    }

    AdaptiveAction RTShadowProfiler::evaluateBudget()
    {
        AdaptiveAction action{};

        if (!adaptiveEnabled || !emaInitialized) return action;

        // VK-1479 C5: the degrade/upgrade decision is now the pure, CPU-testable evaluateBudgetCore
        // (validated by test_rt_shadow_budget). This method only marshals current state in, applies
        // the result, and translates it to the AdaptiveAction the caller consumes.
        BudgetInputs in{};
        in.adaptiveEnabled = adaptiveEnabled;
        in.emaInitialized = emaInitialized;
        in.emaTotalMs = emaTotalMs;
        in.budgetMs = budgetMs;
        in.restoreThreshold = RESTORE_THRESHOLD;
        in.framesOverBudget = consecutiveOverBudget;
        in.framesUnderBudget = consecutiveUnderBudget;
        in.hysteresisFramesDown = HYSTERESIS_FRAMES_DOWN;
        in.hysteresisFramesUp = HYSTERESIS_FRAMES_UP;
        in.baseMaxRayDistance = baseMaxRayDistance;
        in.appliedMaxRayDistance = appliedMaxRayDistance;
        in.baseSpatialPasses = baseSpatialPasses;
        in.appliedSpatialPasses = appliedSpatialPasses;
        in.spotActive = spotActive;
        in.baseSpotBudget = baseSpotBudget;
        in.appliedSpotBudget = appliedSpotBudget;
        in.pointActive = pointActive;
        in.basePointBudget = basePointBudget;
        in.appliedPointBudget = appliedPointBudget;
        in.skipActive = skipNextFrame;

        BudgetDecision d = evaluateBudgetCore(in);

        // Write applied state back so the next evaluation uses current values.
        appliedMaxRayDistance = d.appliedMaxRayDistance;
        appliedSpatialPasses = d.appliedSpatialPasses;
        appliedSpotBudget = d.appliedSpotBudget;
        appliedPointBudget = d.appliedPointBudget;
        skipNextFrame = d.skipActive;
        throttled = d.throttled;
        consecutiveOverBudget = d.framesOverBudget;
        consecutiveUnderBudget = d.framesUnderBudget;

        action.newMaxRayDistance = d.newMaxRayDistance;
        action.newSpatialPasses = d.newSpatialPasses;
        action.newSpotBudget = d.newSpotBudget;
        action.newPointBudget = d.newPointBudget;
        action.skipFrame = d.skipFrame;
        return action;
    }

    void RTShadowProfiler::setBaseSettings(float maxRayDist, int spatialPasses)
    {
        baseMaxRayDistance = maxRayDist;
        baseSpatialPasses = spatialPasses;

        // Reset applied values when user changes base settings
        appliedMaxRayDistance = maxRayDist;
        appliedSpatialPasses = spatialPasses;
        throttled = false;
        skipNextFrame = false;
        consecutiveOverBudget = 0;
        consecutiveUnderBudget = 0;
    }

    void RTShadowProfiler::applyBudgetSettings(const types::RTShadowSettings& settings)
    {
        budgetMs = settings.budgetMs;
        adaptiveEnabled = settings.adaptiveBudgetEnabled;
        asMemoryBudgetBytes = settings.asMemoryBudgetMB * 1024.0f * 1024.0f;

        // VK-1479 C5: capture the RT spot/point light-count budgets so the adaptive path can shrink
        // them when RT shadows blow the frame budget. Applied resets to base on a settings change,
        // mirroring how setBaseSettings resets the directional applied values.
        spotActive = settings.spotEnabled;
        pointActive = settings.pointEnabled;
        baseSpotBudget = settings.spotBudget;
        basePointBudget = settings.pointBudget;
        appliedSpotBudget = settings.spotBudget;
        appliedPointBudget = settings.pointBudget;
    }

    types::RTShadowStats RTShadowProfiler::getStats(const ASMemoryBudget& asBudget) const
    {
        types::RTShadowStats stats{};
        stats.rayDispatchMs = emaRayMs;
        stats.denoiserMs = emaDenoiserMs;
        stats.totalRTShadowMs = emaTotalMs;
        stats.blasBuildMs = emaBlasBuildMs;
        stats.tlasBuildMs = emaTlasBuildMs;

        stats.blasTotalBytes = asBudget.blasTotalBytes;
        stats.tlasTotalBytes = asBudget.tlasTotalBytes;
        stats.scratchPeakBytes = asBudget.scratchPeakBytes;
        stats.blasCount = asBudget.blasCount;
        stats.tlasInstanceCount = asBudget.tlasInstanceCount;
        stats.asMemoryOverBudget = asOverBudget;

        stats.budgetMs = budgetMs;
        stats.currentMaxRayDistance = appliedMaxRayDistance;
        stats.currentSpatialPasses = appliedSpatialPasses;
        stats.isThrottled = throttled;
        stats.framesOverBudget = consecutiveOverBudget;
        stats.framesUnderBudget = consecutiveUnderBudget;

        return stats;
    }
}
