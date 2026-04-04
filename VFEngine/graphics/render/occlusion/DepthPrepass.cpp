#include "DepthPrepass.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "print/Log.hpp"

namespace render::occlusion
{
    DepthPrepass::DepthPrepass(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    DepthPrepass::~DepthPrepass()
    {
        cleanup();
    }

    void DepthPrepass::init(uint32_t w, uint32_t h)
    {
        width = w;
        height = h;

        createImages();

        initialized = true;
        vfLogInfo("Depth prepass initialized: {}x{} (with normal output, dynamic rendering)", width, height);
    }

    void DepthPrepass::createImages()
    {
        // Depth image
        core::ImageInfoRequest depthRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            width, height, 1, 1,
            DEPTH_FORMAT,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(depthRequest, depthImage, depthAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest depthViewRequest(
            device.getLogicalDevice(), depthImage,
            DEPTH_FORMAT, vk::ImageAspectFlagBits::eDepth,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(depthViewRequest, depthImageView);

        // Normal image (world-space normals)
        core::ImageInfoRequest normalRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            width, height, 1, 1,
            NORMAL_FORMAT,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(normalRequest, normalImage, normalAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest normalViewRequest(
            device.getLogicalDevice(), normalImage,
            NORMAL_FORMAT, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(normalViewRequest, normalImageView);

        transitionInitialLayouts();
    }

    void DepthPrepass::transitionInitialLayouts()
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        vk::CommandPool tempPool = device.getLogicalDevice().createCommandPool(poolInfo);

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = tempPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;
        vk::CommandBuffer cmd = device.getLogicalDevice().allocateCommandBuffers(allocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        // Transition depth image
        vk::ImageMemoryBarrier depthBarrier{};
        depthBarrier.oldLayout = vk::ImageLayout::eUndefined;
        depthBarrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = depthImage;
        depthBarrier.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
        depthBarrier.srcAccessMask = vk::AccessFlagBits::eNone;
        depthBarrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                     vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        // Transition normal image
        vk::ImageMemoryBarrier normalBarrier{};
        normalBarrier.oldLayout = vk::ImageLayout::eUndefined;
        normalBarrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        normalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        normalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        normalBarrier.image = normalImage;
        normalBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        normalBarrier.srcAccessMask = vk::AccessFlagBits::eNone;
        normalBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        std::array<vk::ImageMemoryBarrier, 2> barriers = {depthBarrier, normalBarrier};
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eColorAttachmentOutput,
            {}, {}, {}, barriers);

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        device.submitGraphics(submitInfo);
        device.waitGraphicsIdle();

        device.getLogicalDevice().destroyCommandPool(tempPool);
    }

    void DepthPrepass::beginPass(vk::CommandBuffer cmd) const
    {
        core::DynamicRenderingInfo info{};
        info.extent = vk::Extent2D{width, height};
        info.colorAttachments = {core::colorClear(normalImageView)};
        info.depthAttachment = core::depthClear(depthImageView, 1.0f, 0);
        info.hasDepth = true;

        core::beginDynamicRendering(cmd, info);

        vk::Viewport viewport{0.0f, 0.0f,
            static_cast<float>(width), static_cast<float>(height),
            0.0f, 1.0f};
        cmd.setViewport(0, viewport);

        vk::Rect2D scissor{{0, 0}, {width, height}};
        cmd.setScissor(0, scissor);
    }

    void DepthPrepass::endPass(vk::CommandBuffer cmd) const
    {
        core::endDynamicRendering(cmd);
    }

    void DepthPrepass::cleanup()
    {
        if (!initialized) return;

        device.getLogicalDevice().waitIdle();

        device.getLogicalDevice().destroyImageView(depthImageView);
        device.getLogicalDevice().destroyImage(depthImage);
        device.getMemoryManager().free(depthAllocation);
        depthAllocation = {};

        device.getLogicalDevice().destroyImageView(normalImageView);
        device.getLogicalDevice().destroyImage(normalImage);
        device.getMemoryManager().free(normalAllocation);
        normalAllocation = {};

        initialized = false;
    }
}
