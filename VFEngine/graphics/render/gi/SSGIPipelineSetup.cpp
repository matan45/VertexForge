#include "SSGIPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"

namespace render::gi
{
    void SSGIPipeline::createTraceRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = SSGI_FORMAT;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = {};
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        traceRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void SSGIPipeline::createTemporalRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = SSGI_FORMAT;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = {};
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        temporalRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void SSGIPipeline::createDenoiseRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = SSGI_FORMAT;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = {};
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        denoiseRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void SSGIPipeline::createCompositeRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead
                                 | vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        compositeRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void SSGIPipeline::createTraceFramebuffer()
    {
        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = traceRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &ssgiRawImageView;
        fbInfo.width = traceExtent.width;
        fbInfo.height = traceExtent.height;
        fbInfo.layers = 1;

        traceFramebuffer = device.getLogicalDevice().createFramebuffer(fbInfo);
    }

    void SSGIPipeline::createTemporalFramebuffer()
    {
        for (uint32_t i = 0; i < 2; ++i)
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = temporalRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &ssgiHistoryImageViews[i];
            fbInfo.width = traceExtent.width;
            fbInfo.height = traceExtent.height;
            fbInfo.layers = 1;

            temporalFramebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void SSGIPipeline::createDenoiseFramebuffer()
    {
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = denoiseRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &ssgiDenoiseHorizImageView;
            fbInfo.width = traceExtent.width;
            fbInfo.height = traceExtent.height;
            fbInfo.layers = 1;

            denoiseHorizFramebuffer = device.getLogicalDevice().createFramebuffer(fbInfo);
        }

        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = denoiseRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &ssgiDenoisedImageView;
            fbInfo.width = traceExtent.width;
            fbInfo.height = traceExtent.height;
            fbInfo.layers = 1;

            denoiseFramebuffer = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void SSGIPipeline::createCompositeFramebuffers()
    {
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
        compositeFramebuffers.resize(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = compositeRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &offscreenResources.colorImages[i].colorImageView;
            fbInfo.width = currentExtent.width;
            fbInfo.height = currentExtent.height;
            fbInfo.layers = 1;

            compositeFramebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void SSGIPipeline::createTracePipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {traceSet0Layout, traceSet1Layout};

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        tracePipelineLayout = dev.createPipelineLayout(layoutInfo);
        tracePipeline = createFullscreenPipeline(tracePipelineLayout, traceRenderPass,
                                                  traceExtent, traceShader, false);
    }

    void SSGIPipeline::createTemporalPipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {temporalSet0Layout, temporalSet1Layout};

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        temporalPipelineLayout = dev.createPipelineLayout(layoutInfo);
        temporalPipeline = createFullscreenPipeline(temporalPipelineLayout, temporalRenderPass,
                                                     traceExtent, temporalShader, false);
    }

    void SSGIPipeline::createDenoisePipeline()
    {
        auto& dev = device.getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(DenoisePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &denoiseSet0Layout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        denoisePipelineLayout = dev.createPipelineLayout(layoutInfo);
        denoisePipeline = createFullscreenPipeline(denoisePipelineLayout, denoiseRenderPass,
                                                    traceExtent, denoiseShader, false);
    }

    void SSGIPipeline::createCompositePipeline()
    {
        auto& dev = device.getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(CompositePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &compositeSet0Layout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        compositePipelineLayout = dev.createPipelineLayout(layoutInfo);
        compositePipeline = createFullscreenPipeline(compositePipelineLayout, compositeRenderPass,
                                                      currentExtent, compositeShader, true);
    }
}
