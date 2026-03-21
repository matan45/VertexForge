#include "DepthPrepass.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/ImageUtilities.hpp"
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

        createDepthImage();
        createRenderPass();
        createFramebuffer();

        initialized = true;
        vfLogInfo("Depth prepass initialized: {}x{}", width, height);
    }

    void DepthPrepass::createDepthImage()
    {
        core::ImageInfoRequest imageRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            width, height, 1, 1,
            vk::Format::eD32Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageRequest, depthImage, depthMemory);

        core::ImageViewInfoRequest viewRequest(
            device.getLogicalDevice(), depthImage,
            vk::Format::eD32Sfloat, vk::ImageAspectFlagBits::eDepth,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(viewRequest, depthImageView);

        transitionInitialLayout();
    }

    void DepthPrepass::transitionInitialLayout()
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

        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = depthImage;
        barrier.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            {}, {}, {}, barrier);

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        device.submitGraphics(submitInfo);
        device.waitGraphicsIdle();

        device.getLogicalDevice().destroyCommandPool(tempPool);
    }

    void DepthPrepass::createRenderPass()
    {
        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = vk::Format::eD32Sfloat;
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 0;
        subpass.pDepthStencilAttachment = &depthRef;

        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dependency.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependency.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                   vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &depthAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
    }

    void DepthPrepass::createFramebuffer()
    {
        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &depthImageView;
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;

        framebuffer = device.getLogicalDevice().createFramebuffer(fbInfo);
    }

    void DepthPrepass::beginPass(vk::CommandBuffer cmd) const
    {
        vk::ClearValue clearValue{};
        clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        vk::RenderPassBeginInfo rpInfo{};
        rpInfo.renderPass = renderPass;
        rpInfo.framebuffer = framebuffer;
        rpInfo.renderArea.offset = vk::Offset2D{0, 0};
        rpInfo.renderArea.extent = vk::Extent2D{width, height};
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &clearValue;

        cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);

        vk::Viewport viewport{0.0f, 0.0f,
            static_cast<float>(width), static_cast<float>(height),
            0.0f, 1.0f};
        cmd.setViewport(0, viewport);

        vk::Rect2D scissor{{0, 0}, {width, height}};
        cmd.setScissor(0, scissor);
    }

    void DepthPrepass::endPass(vk::CommandBuffer cmd) const
    {
        cmd.endRenderPass();
    }

    void DepthPrepass::cleanup()
    {
        if (!initialized) return;

        device.getLogicalDevice().waitIdle();

        device.getLogicalDevice().destroyFramebuffer(framebuffer);
        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyImageView(depthImageView);
        device.getLogicalDevice().freeMemory(depthMemory);
        device.getLogicalDevice().destroyImage(depthImage);

        initialized = false;
    }
}
