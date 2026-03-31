#include "RTShadowProfiler.hpp"
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
    }

    void RTShadowProfiler::writeTimestamp(vk::CommandBuffer cmd, uint32_t frameIndex,
                                           RTShadowTimestamp slot, vk::PipelineStageFlagBits2 stage)
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

        std::vector<uint64_t> timestamps;
        if (!queryPool.readResults(logicalDevice, frameIndex, timestamps)) return;

        // Ray dispatch timing
        float rayMs = queryPool.toMilliseconds(
            timestamps[RTShadowTimestamp::BeforeRayDispatch],
            timestamps[RTShadowTimestamp::AfterRayDispatch]);

        // Denoiser timing (AfterRayDispatch -> AfterDenoiser)
        float denoiserMs = queryPool.toMilliseconds(
            timestamps[RTShadowTimestamp::AfterRayDispatch],
            timestamps[RTShadowTimestamp::AfterDenoiser]);

        float totalMs = rayMs + denoiserMs;

        updateEMA(rayMs, emaRayMs);
        updateEMA(denoiserMs, emaDenoiserMs);
        updateEMA(totalMs, emaTotalMs);

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

        const_cast<RTShadowProfiler*>(this)->emaInitialized = true;

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

    AdaptiveAction RTShadowProfiler::evaluateBudget() const
    {
        AdaptiveAction action{};

        if (!adaptiveEnabled || !emaInitialized) return action;

        // Throttle down: reduce quality when over budget for sustained period
        if (consecutiveOverBudget >= HYSTERESIS_FRAMES_DOWN)
        {
            if (appliedSpatialPasses > 1)
            {
                action.newSpatialPasses = appliedSpatialPasses - 1;
            }
            else if (appliedMaxRayDistance > baseMaxRayDistance * 0.25f)
            {
                action.newMaxRayDistance = appliedMaxRayDistance * 0.75f;
            }
            else
            {
                action.skipFrame = true;
            }
        }
        // Restore quality when under budget for sustained period
        else if (consecutiveUnderBudget >= HYSTERESIS_FRAMES_UP)
        {
            if (skipNextFrame)
            {
                // Un-skip frames first
                action.skipFrame = false;
            }
            else if (appliedMaxRayDistance < baseMaxRayDistance)
            {
                // Gradual restore: 10% toward base
                float step = (baseMaxRayDistance - appliedMaxRayDistance) * 0.1f;
                action.newMaxRayDistance = appliedMaxRayDistance + std::max(step, 10.0f);
                if (action.newMaxRayDistance.value() > baseMaxRayDistance)
                    action.newMaxRayDistance = baseMaxRayDistance;
            }
            else if (appliedSpatialPasses < baseSpatialPasses)
            {
                action.newSpatialPasses = appliedSpatialPasses + 1;
            }
        }

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
