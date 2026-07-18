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

        /// Write a timestamp at the given pipeline stage (Vulkan 1.0, no synchronization2 needed).
        void writeTimestamp(vk::CommandBuffer cmd, uint32_t frameIndex,
                           uint32_t queryIndex, vk::PipelineStageFlagBits stage);

        /// Read results from a completed frame.
        /// Returns true if results are available, fills outTimestamps.
        /// count limits the read to the first `count` queries (0 ⇒ full queryCount);
        /// reading only the queries actually written this frame avoids a permanent
        /// VK_NOT_READY when the pool is larger than the written range.
        bool readResults(const vk::Device& logicalDevice, uint32_t frameIndex,
                         std::vector<uint64_t>& outTimestamps, uint32_t count = 0) const;

        /// Convert a timestamp delta to milliseconds.
        float toMilliseconds(uint64_t startTimestamp, uint64_t endTimestamp) const;

        bool isValid() const { return valid; }
        uint32_t getQueryCount() const { return queryCount; }
        double getTimestampPeriod() const { return static_cast<double>(timestampPeriod); }
        uint32_t getTimestampValidBits() const { return timestampValidBits; }

    private:
        vk::QueryPool pools[core::MAX_FRAMES_IN_FLIGHT]{};
        uint32_t queryCount = 0;
        float timestampPeriod = 0.0f; // nanoseconds per tick
        // Meaningful low bits of each timestamp (spec allows 36..64, and guarantees
        // the rest read as zero). The counter wraps at 2^timestampValidBits.
        uint32_t timestampValidBits = 0;
        bool valid = false;
    };
}
