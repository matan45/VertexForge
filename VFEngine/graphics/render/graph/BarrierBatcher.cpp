#include "BarrierBatcher.hpp"

namespace render::graph
{
    void BarrierBatcher::add(uint32_t passIndex, const vk::ImageMemoryBarrier2& barrier)
    {
        pendingBarriers[passIndex].push_back(barrier);
    }

    void BarrierBatcher::flush(vk::CommandBuffer cmd, uint32_t passIndex)
    {
        auto it = pendingBarriers.find(passIndex);
        if (it == pendingBarriers.end() || it->second.empty())
            return;

        auto& barriers = it->second;

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        depInfo.pImageMemoryBarriers = barriers.data();

        cmd.pipelineBarrier2KHR(depInfo);

        totalBarrierCount += static_cast<uint32_t>(barriers.size());
        totalFlushCount++;

        barriers.clear();
    }

    void BarrierBatcher::clear()
    {
        pendingBarriers.clear();
        totalBarrierCount = 0;
        totalFlushCount = 0;
    }
}
