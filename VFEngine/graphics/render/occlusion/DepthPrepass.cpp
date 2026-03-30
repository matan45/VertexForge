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

        createImages();
        createRenderPass();
        createFramebuffer();

        initialized = true;
        vfLogInfo("Depth prepass initialized: {}x{} (with normal output)", width, height);
    }

    void DepthPrepass::createImages()
    {
        // Depth image (unchanged)
        core::ImageInfoRequest depthRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            width, height, 1, 1,
            vk::Format::eD32Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(depthRequest, depthImage, depthAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest depthViewRequest(
            device.getLogicalDevice(), depthImage,
            vk::Format::eD32Sfloat, vk::ImageAspectFlagBits::eDepth,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(depthViewRequest, depthImageView);

        // Normal image (world-space normals)
        core::ImageInfoRequest normalRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            width, height, 1, 1,
            vk::Format::eR16G16B16A16Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(normalRequest, normalImage, normalAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest normalViewRequest(
            device.getLogicalDevice(), normalImage,
            vk::Format::eR16G16B16A16Sfloat, vk::ImageAspectFlagBits::eColor,
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

    void DepthPrepass::createRenderPass()
    {
        // Attachment 0: Depth
        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = vk::Format::eD32Sfloat;
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        // Attachment 1: World-space normals
        vk::AttachmentDescription normalAttachment{};
        normalAttachment.format = vk::Format::eR16G16B16A16Sfloat;
        normalAttachment.samples = vk::SampleCountFlagBits::e1;
        normalAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        normalAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        normalAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        normalAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        normalAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        normalAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        std::array<vk::AttachmentDescription, 2> attachments = {depthAttachment, normalAttachment};

        vk::AttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference normalRef{};
        normalRef.attachment = 1;
        normalRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &normalRef;
        subpass.pDepthStencilAttachment = &depthRef;

        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests |
                                  vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                  vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                                   vk::AccessFlagBits::eColorAttachmentWrite;
        dependency.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                   vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                                   vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
    }

    void DepthPrepass::createFramebuffer()
    {
        std::array<vk::ImageView, 2> attachments = {depthImageView, normalImageView};

        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;

        framebuffer = device.getLogicalDevice().createFramebuffer(fbInfo);
    }

    void DepthPrepass::beginPass(vk::CommandBuffer cmd) const
    {
        std::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        clearValues[1].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

        vk::RenderPassBeginInfo rpInfo{};
        rpInfo.renderPass = renderPass;
        rpInfo.framebuffer = framebuffer;
        rpInfo.renderArea.offset = vk::Offset2D{0, 0};
        rpInfo.renderArea.extent = vk::Extent2D{width, height};
        rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpInfo.pClearValues = clearValues.data();

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
