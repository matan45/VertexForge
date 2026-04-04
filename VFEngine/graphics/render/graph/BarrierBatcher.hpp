#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vector>
#include <unordered_map>

namespace render::graph
{
    // Collects ImageMemoryBarrier2 structs per-pass and flushes them as a single
    // pipelineBarrier2KHR call before each pass that needs barriers.
    class BarrierBatcher
    {
    public:
        // Queue a barrier to be flushed before the given pass executes
        void add(uint32_t passIndex, const vk::ImageMemoryBarrier2& barrier);

        // Flush all barriers queued for the given pass as a single pipelineBarrier2KHR call
        void flush(vk::CommandBuffer cmd, uint32_t passIndex);

        void clear();

        uint32_t getTotalBarrierCount() const { return totalBarrierCount; }
        uint32_t getTotalFlushCount() const { return totalFlushCount; }

    private:
        std::unordered_map<uint32_t, std::vector<vk::ImageMemoryBarrier2>> pendingBarriers;
        uint32_t totalBarrierCount = 0;
        uint32_t totalFlushCount = 0;
    };
}
