#pragma once

#include "RenderGraphTypes.hpp"
#include <unordered_map>
#include <optional>

namespace render::graph
{
    // Tracks per-resource synchronization state across passes
    struct ResourceState
    {
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
        vk::PipelineStageFlags2 lastStage = vk::PipelineStageFlagBits2::eNone;
        vk::AccessFlags2 lastAccess = vk::AccessFlagBits2::eNone;
        uint32_t lastQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        bool wasWritten = false;
    };

    class ResourceTracker
    {
    public:
        void initResource(uint32_t resourceIndex, vk::ImageLayout initialLayout);

        // Transition a resource to a new state. Returns a barrier if one is needed.
        std::optional<vk::ImageMemoryBarrier2> transition(
            uint32_t resourceIndex,
            vk::PipelineStageFlags2 dstStage,
            vk::AccessFlags2 dstAccess,
            vk::ImageLayout dstLayout,
            bool isWrite,
            vk::Image image,
            vk::ImageAspectFlags aspectMask);

        // Force the tracked state without emitting a barrier (for opaque passes)
        void forceState(uint32_t resourceIndex, vk::ImageLayout layout,
                        vk::PipelineStageFlags2 stage, vk::AccessFlags2 access, bool isWrite);

        void reset();

        const ResourceState* getState(uint32_t resourceIndex) const;

    private:
        std::unordered_map<uint32_t, ResourceState> states;
    };
}
