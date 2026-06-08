#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vector>
#include <optional>

namespace core
{
    // --- Attachment info factories ---

    // --- MSAA resolve helpers ---
    // When `resolveView` is non-null the multisampled attachment resolves into it at
    // store time (color: average; depth: sample zero). Single source for the scoped
    // MSAA path so the multisampled scene targets resolve into the existing
    // single-sample targets that every downstream pass already reads.

    inline vk::RenderingAttachmentInfo colorClear(vk::ImageView view, vk::ClearColorValue clearValue,
                                                  vk::ImageView resolveView = nullptr)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        info.loadOp = vk::AttachmentLoadOp::eClear;
        info.storeOp = vk::AttachmentStoreOp::eStore;
        info.clearValue.color = clearValue;
        if (resolveView)
        {
            info.resolveMode = vk::ResolveModeFlagBits::eAverage;
            info.resolveImageView = resolveView;
            info.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        }
        return info;
    }

    inline vk::RenderingAttachmentInfo colorLoad(vk::ImageView view, vk::ImageView resolveView = nullptr)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        info.loadOp = vk::AttachmentLoadOp::eLoad;
        info.storeOp = vk::AttachmentStoreOp::eStore;
        if (resolveView)
        {
            info.resolveMode = vk::ResolveModeFlagBits::eAverage;
            info.resolveImageView = resolveView;
            info.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        }
        return info;
    }

    inline vk::RenderingAttachmentInfo colorDontCare(vk::ImageView view)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        info.loadOp = vk::AttachmentLoadOp::eDontCare;
        info.storeOp = vk::AttachmentStoreOp::eStore;
        return info;
    }

    inline vk::RenderingAttachmentInfo depthClear(vk::ImageView view,
                                                   float clearDepth = 1.0f, uint32_t clearStencil = 0,
                                                   vk::ImageView resolveView = nullptr)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        info.loadOp = vk::AttachmentLoadOp::eClear;
        info.storeOp = vk::AttachmentStoreOp::eStore;
        info.clearValue.depthStencil = vk::ClearDepthStencilValue{clearDepth, clearStencil};
        if (resolveView)
        {
            // SampleZero is the universally-supported depth/stencil resolve mode.
            info.resolveMode = vk::ResolveModeFlagBits::eSampleZero;
            info.resolveImageView = resolveView;
            info.resolveImageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        }
        return info;
    }

    inline vk::RenderingAttachmentInfo depthLoad(vk::ImageView view)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        info.loadOp = vk::AttachmentLoadOp::eLoad;
        info.storeOp = vk::AttachmentStoreOp::eStore;
        return info;
    }

    inline vk::RenderingAttachmentInfo stencilClear(vk::ImageView view, uint32_t clearStencil = 0)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        info.loadOp = vk::AttachmentLoadOp::eClear;
        info.storeOp = vk::AttachmentStoreOp::eStore;
        info.clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, clearStencil};
        return info;
    }

    inline vk::RenderingAttachmentInfo stencilLoad(vk::ImageView view)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        info.loadOp = vk::AttachmentLoadOp::eLoad;
        info.storeOp = vk::AttachmentStoreOp::eStore;
        return info;
    }

    inline vk::RenderingAttachmentInfo depthReadOnly(vk::ImageView view)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        info.loadOp = vk::AttachmentLoadOp::eLoad;
        info.storeOp = vk::AttachmentStoreOp::eNone;
        return info;
    }

    // --- Swapchain layout transitions (synchronization2) ---

    inline void transitionSwapchainForRendering(vk::CommandBuffer cmd, vk::Image swapchainImage)
    {
        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
        barrier.srcAccessMask = {};
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        barrier.image = swapchainImage;
        barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        cmd.pipelineBarrier2KHR(depInfo);
    }

    inline void transitionSwapchainForPresent(vk::CommandBuffer cmd, vk::Image swapchainImage)
    {
        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
        barrier.dstAccessMask = {};
        barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
        barrier.image = swapchainImage;
        barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        cmd.pipelineBarrier2KHR(depInfo);
    }

    // --- Dynamic rendering begin/end ---

    struct DynamicRenderingInfo
    {
        vk::Extent2D extent{};
        vk::Offset2D offset{0, 0};
        uint32_t layerCount = 1;
        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        std::optional<vk::RenderingAttachmentInfo> depthAttachment;
        std::optional<vk::RenderingAttachmentInfo> stencilAttachment;
    };

    inline void beginDynamicRendering(vk::CommandBuffer cmd, const DynamicRenderingInfo& info)
    {
        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea.offset = info.offset;
        renderingInfo.renderArea.extent = info.extent;
        renderingInfo.layerCount = info.layerCount;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(info.colorAttachments.size());
        renderingInfo.pColorAttachments = info.colorAttachments.empty() ? nullptr : info.colorAttachments.data();
        if (info.depthAttachment.has_value())
            renderingInfo.pDepthAttachment = &info.depthAttachment.value();
        if (info.stencilAttachment.has_value())
            renderingInfo.pStencilAttachment = &info.stencilAttachment.value();

        cmd.beginRendering(renderingInfo);
    }

    inline void endDynamicRendering(vk::CommandBuffer cmd)
    {
        cmd.endRendering();
    }
}
