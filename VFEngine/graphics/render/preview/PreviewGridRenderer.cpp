#include "PreviewGridRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"

namespace render::preview
{
    PreviewGridRenderer::PreviewGridRenderer(core::Device& device, core::SwapChain& swapChain,
                                              core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
        , gridRenderer{std::make_unique<mesh::GridRenderer>(device, swapChain)}
    {
    }

    PreviewGridRenderer::~PreviewGridRenderer() = default;

    void PreviewGridRenderer::init()
    {
        createRenderPass();
        createFramebuffers();
        gridRenderer->init(renderPass);
        initialized = true;
    }

    void PreviewGridRenderer::recreate()
    {
        for (auto fb : framebuffers)
            device.getLogicalDevice().destroyFramebuffer(fb);
        device.getLogicalDevice().destroyRenderPass(renderPass);

        createRenderPass();
        createFramebuffers();
        gridRenderer->recreate(renderPass);
    }

    void PreviewGridRenderer::cleanUp()
    {
        if (!initialized) return;

        gridRenderer->cleanUp();

        for (auto fb : framebuffers)
            device.getLogicalDevice().destroyFramebuffer(fb);
        framebuffers.clear();

        device.getLogicalDevice().destroyRenderPass(renderPass);
        initialized = false;
    }

    void PreviewGridRenderer::cleanUpShader()
    {
        gridRenderer->cleanUpShader();
    }

    void PreviewGridRenderer::createRenderPass()
    {
        // Color attachment - load existing content
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        // Depth attachment - load existing depth for proper occlusion
        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = swapChain.getSwapchainDepthStencilFormat();
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        rpInfo.pAttachments = attachments.data();
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void PreviewGridRenderer::createFramebuffers()
    {
        framebuffers.resize(offscreenResources.colorImages.size());
        vk::ImageView depth = offscreenResources.depthImage.depthImageView;

        for (uint32_t i = 0; i < framebuffers.size(); ++i)
        {
            std::array<vk::ImageView, 2> attachments = {
                offscreenResources.colorImages[i].colorImageView,
                depth
            };

            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            fbInfo.pAttachments = attachments.data();
            fbInfo.width = swapChain.getSwapchainExtent().width;
            fbInfo.height = swapChain.getSwapchainExtent().height;
            fbInfo.layers = 1;

            framebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void PreviewGridRenderer::render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                      const glm::mat4& view, const glm::mat4& projection,
                                      bool visible) const
    {
        if (!initialized || !visible) return;

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffers[imageIndex];
        rpBegin.renderArea.extent = swapChain.getSwapchainExtent();

        commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
        gridRenderer->render(commandBuffer, view, projection);
        commandBuffer.endRenderPass();
    }
}
