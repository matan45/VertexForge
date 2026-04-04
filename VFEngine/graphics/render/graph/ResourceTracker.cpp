#include "ResourceTracker.hpp"

namespace render::graph
{
    void ResourceTracker::initResource(uint32_t resourceIndex, vk::ImageLayout initialLayout)
    {
        ResourceState state{};
        state.layout = initialLayout;
        states[resourceIndex] = state;
    }

    std::optional<vk::ImageMemoryBarrier2> ResourceTracker::transition(
        uint32_t resourceIndex,
        vk::PipelineStageFlags2 dstStage,
        vk::AccessFlags2 dstAccess,
        vk::ImageLayout dstLayout,
        bool isWrite,
        vk::Image image,
        vk::ImageAspectFlags aspectMask)
    {
        auto it = states.find(resourceIndex);
        if (it == states.end())
        {
            // Unknown resource, initialize with undefined layout
            initResource(resourceIndex, vk::ImageLayout::eUndefined);
            it = states.find(resourceIndex);
        }

        auto& state = it->second;

        // Read-after-read with same layout: no barrier needed
        if (!isWrite && !state.wasWritten && state.layout == dstLayout)
        {
            return std::nullopt;
        }

        // Same layout, no prior write, and this is a read: skip
        bool needsBarrier = false;
        if (state.layout != dstLayout)
        {
            needsBarrier = true; // Layout transition required
        }
        else if (state.wasWritten)
        {
            needsBarrier = true; // Write-after-read or read-after-write
        }
        else if (isWrite)
        {
            needsBarrier = true; // Write needs barrier for ordering
        }

        if (!needsBarrier)
        {
            return std::nullopt;
        }

        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = state.lastStage;
        barrier.srcAccessMask = state.lastAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = state.layout;
        barrier.newLayout = dstLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

        // Handle initial undefined layout: no src stage/access needed
        if (state.layout == vk::ImageLayout::eUndefined)
        {
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
            barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
        }

        // Update tracked state
        state.layout = dstLayout;
        state.lastStage = dstStage;
        state.lastAccess = dstAccess;
        state.wasWritten = isWrite;

        return barrier;
    }

    void ResourceTracker::reset()
    {
        states.clear();
    }

    const ResourceState* ResourceTracker::getState(uint32_t resourceIndex) const
    {
        auto it = states.find(resourceIndex);
        return it != states.end() ? &it->second : nullptr;
    }
}
