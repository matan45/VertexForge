#pragma once

#include "../../core/GraphicsConstants.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

namespace render::raytracing
{
    /// Reusable Vulkan GPU timestamp query pool.
    /// Double-buffered per frame-in-flight so readback never races with writes.
    class GPUTimestampQueryPool
    {
    public:
        GPUTimestampQueryPool() = default;
        ~GPUTimestampQueryPool() = default;

        GPUTimestampQueryPool(const GPUTimestampQueryPool&) = delete;
        GPUTimestampQueryPool& operator=(const GPUTimestampQueryPool&) = delete;

        /// Create query pools. Returns false if timestamps are not supported.
        bool init(core::Device& device, uint32_t numQueries);
        void cleanup(const vk::Device& logicalDevice);

        /// Reset the pool for this frame index. Must be called before any writeTimestamp.
        void resetFrame(vk::CommandBuffer cmd, uint32_t frameIndex);

        /// Write a timestamp at the given pipeline stage.
        void writeTimestamp(vk::CommandBuffer cmd, uint32_t frameIndex,
                           uint32_t queryIndex, vk::PipelineStageFlagBits2 stage);

        /// Read results from a completed frame.
        /// Returns true if results are available, fills outTimestamps.
        bool readResults(const vk::Device& logicalDevice, uint32_t frameIndex,
                         std::vector<uint64_t>& outTimestamps) const;

        /// Convert a timestamp delta to milliseconds.
        float toMilliseconds(uint64_t startTimestamp, uint64_t endTimestamp) const;

        bool isValid() const { return valid; }
        uint32_t getQueryCount() const { return queryCount; }

    private:
        vk::QueryPool pools[core::MAX_FRAMES_IN_FLIGHT]{};
        uint32_t queryCount = 0;
        float timestampPeriod = 0.0f; // nanoseconds per tick
        bool valid = false;
    };
}
