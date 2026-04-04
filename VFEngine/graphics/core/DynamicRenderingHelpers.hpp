#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vector>
#include <optional>

namespace core
{
    // --- Attachment info factories ---

    inline vk::RenderingAttachmentInfo colorClear(vk::ImageView view, vk::ClearColorValue clearValue)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        info.loadOp = vk::AttachmentLoadOp::eClear;
        info.storeOp = vk::AttachmentStoreOp::eStore;
        info.clearValue.color = clearValue;
        return info;
    }

    inline vk::RenderingAttachmentInfo colorLoad(vk::ImageView view)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        info.loadOp = vk::AttachmentLoadOp::eLoad;
        info.storeOp = vk::AttachmentStoreOp::eStore;
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
                                                   float clearDepth = 1.0f, uint32_t clearStencil = 0)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        info.loadOp = vk::AttachmentLoadOp::eClear;
        info.storeOp = vk::AttachmentStoreOp::eStore;
        info.clearValue.depthStencil = vk::ClearDepthStencilValue{clearDepth, clearStencil};
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

    inline vk::RenderingAttachmentInfo depthReadOnly(vk::ImageView view)
    {
        vk::RenderingAttachmentInfo info{};
        info.imageView = view;
        info.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        info.loadOp = vk::AttachmentLoadOp::eLoad;
        info.storeOp = vk::AttachmentStoreOp::eNone;
        return info;
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
