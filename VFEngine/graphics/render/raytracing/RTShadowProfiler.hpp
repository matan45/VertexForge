#pragma once

#include "GPUTimestampQueryPool.hpp"
#include "AccelerationStructureManager.hpp"
#include "../../../utilities/types/RenderSettings.hpp"
#include <vulkan/vulkan.hpp>
#include <optional>

namespace core
{
    class Device;
}

namespace render::raytracing
{
    /// Timestamp query indices for RT shadow profiling.
    enum RTShadowTimestamp : uint32_t
    {
        BeforeRayDispatch = 0,
        AfterRayDispatch = 1,
        AfterDenoiser = 2,
        BeforeBLASBuild = 3,
        AfterBLASBuild = 4,
        AfterTLASBuild = 5,
        Count = 6
    };

    /// Result of the adaptive budget evaluation — what to change this frame.
    struct AdaptiveAction
    {
        std::optional<float> newMaxRayDistance;
        std::optional<int> newSpatialPasses;
        bool skipFrame = false;
    };

    /// Collects GPU timestamps, computes EMA-smoothed timings,
    /// and drives adaptive quality based on a frame budget.
    class RTShadowProfiler
    {
    public:
        RTShadowProfiler() = default;
        ~RTShadowProfiler() = default;

        RTShadowProfiler(const RTShadowProfiler&) = delete;
        RTShadowProfiler& operator=(const RTShadowProfiler&) = delete;

        bool init(core::Device& device);
        void cleanup(const vk::Device& logicalDevice);

        /// Reset query pool for this frame. Call before any timestamp writes.
        void resetFrame(vk::CommandBuffer cmd, uint32_t frameIndex);

        /// Write a timestamp.
        void writeTimestamp(vk::CommandBuffer cmd, uint32_t frameIndex,
                           RTShadowTimestamp slot, vk::PipelineStageFlagBits stage);

        /// Read back completed frame's results and update EMA.
        /// Call at the start of the frame, before dispatch.
        void readbackAndUpdate(const vk::Device& logicalDevice, uint32_t frameIndex,
                               const ASMemoryBudget& asBudget);

        /// Evaluate the adaptive budget and return recommended changes.
        AdaptiveAction evaluateBudget();

        /// Set the user's base quality settings (before adaptive modification).
        void setBaseSettings(float maxRayDist, int spatialPasses);

        /// Update budget configuration from render settings.
        void applyBudgetSettings(const types::RTShadowSettings& settings);

        /// Build the stats snapshot for UI display.
        types::RTShadowStats getStats(const ASMemoryBudget& asBudget) const;

        /// Mark that BLAS/TLAS builds happened this frame (so their timestamps are valid).
        void markBLASBuilt() { blasBuiltThisFrame = true; }
        void markTLASBuilt() { tlasBuiltThisFrame = true; }

        bool isValid() const { return queryPool.isValid(); }

    private:
        GPUTimestampQueryPool queryPool;

        // EMA state
        static constexpr float EMA_ALPHA = 0.1f;
        float emaRayMs = 0.0f;
        float emaDenoiserMs = 0.0f;
        float emaTotalMs = 0.0f;
        float emaBlasBuildMs = 0.0f;
        float emaTlasBuildMs = 0.0f;
        bool emaInitialized = false;

        // Hysteresis
        static constexpr uint32_t HYSTERESIS_FRAMES_DOWN = 10;
        static constexpr uint32_t HYSTERESIS_FRAMES_UP = 30;
        static constexpr float RESTORE_THRESHOLD = 0.7f;
        uint32_t consecutiveOverBudget = 0;
        uint32_t consecutiveUnderBudget = 0;

        // Budget configuration
        float budgetMs = 2.0f;
        bool adaptiveEnabled = true;
        float asMemoryBudgetBytes = 256.0f * 1024.0f * 1024.0f;

        // Base settings (user-configured, before adaptive modification)
        float baseMaxRayDistance = 500.0f;
        int baseSpatialPasses = 3;

        // Currently applied adaptive values
        float appliedMaxRayDistance = 500.0f;
        int appliedSpatialPasses = 3;
        bool throttled = false;
        bool skipNextFrame = false;

        // Per-frame flags for conditional timestamp reads
        bool blasBuiltThisFrame = false;
        bool tlasBuiltThisFrame = false;

        // Track which frame slots have been reset (avoid reading uninitialized queries)
        bool frameSlotReady[core::MAX_FRAMES_IN_FLIGHT]{};

        // AS memory
        bool asOverBudget = false;

        void updateEMA(float sample, float& ema) const;
    };
}
