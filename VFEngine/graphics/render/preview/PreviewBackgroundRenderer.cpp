#include "PreviewBackgroundRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"

namespace render::preview
{
    PreviewBackgroundRenderer::PreviewBackgroundRenderer(core::Device& device, core::SwapChain& swapChain,
                                                          core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
    {
    }

    PreviewBackgroundRenderer::~PreviewBackgroundRenderer() = default;

    void PreviewBackgroundRenderer::init()
    {
        loadShader();
        createRenderPass();
        createFramebuffers();
        createPipeline();
        initialized = true;
    }

    void PreviewBackgroundRenderer::recreate()
    {
        for (auto fb : framebuffers)
            device.getLogicalDevice().destroyFramebuffer(fb);
        device.getLogicalDevice().destroyPipeline(pipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        device.getLogicalDevice().destroyRenderPass(renderPass);

        createRenderPass();
        createFramebuffers();
        createPipeline();
    }

    void PreviewBackgroundRenderer::cleanUp()
    {
        if (!initialized) return;

        for (auto fb : framebuffers)
            device.getLogicalDevice().destroyFramebuffer(fb);
        framebuffers.clear();

        device.getLogicalDevice().destroyPipeline(pipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        device.getLogicalDevice().destroyRenderPass(renderPass);

        initialized = false;
    }

    void PreviewBackgroundRenderer::cleanUpShader()
    {
        if (shader)
            shader->cleanUp();
    }

    void PreviewBackgroundRenderer::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/preview/gradient_background.glsl");
    }

    void PreviewBackgroundRenderer::createRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSceneColorFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void PreviewBackgroundRenderer::createFramebuffers()
    {
        framebuffers.resize(offscreenResources.colorImages.size());
        for (uint32_t i = 0; i < framebuffers.size(); ++i)
        {
            vk::ImageView view = offscreenResources.colorImages[i].colorImageView;
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &view;
            fbInfo.width = swapChain.getSwapchainExtent().width;
            fbInfo.height = swapChain.getSwapchainExtent().height;
            fbInfo.layers = 1;
            framebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void PreviewBackgroundRenderer::createPipeline()
    {
        // Push constant range
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(GradientPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
        pipelineLayout = device.getLogicalDevice().createPipelineLayout(layoutInfo);

        // No vertex input (fullscreen triangle from gl_VertexIndex)
        vk::PipelineVertexInputStateCreateInfo vertexInput{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{};
        viewport.width = static_cast<float>(swapChain.getSwapchainExtent().width);
        viewport.height = static_cast<float>(swapChain.getSwapchainExtent().height);
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.extent = swapChain.getSwapchainExtent();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                               vk::ColorComponentFlagBits::eG |
                                               vk::ColorComponentFlagBits::eB |
                                               vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // No depth test for fullscreen background
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        auto shaderStages = shader->getShaderStages();

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;

        auto result = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        pipeline = result.value;
    }

    void PreviewBackgroundRenderer::render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                            const glm::vec4& topColor, const glm::vec4& bottomColor) const
    {
        if (!initialized) return;

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffers[imageIndex];
        rpBegin.renderArea.extent = swapChain.getSwapchainExtent();

        commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        GradientPushConstants pushConstants{};
        pushConstants.topColor = topColor;
        pushConstants.bottomColor = bottomColor;

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(GradientPushConstants), &pushConstants);

        commandBuffer.draw(3, 1, 0, 0); // Fullscreen triangle

        commandBuffer.endRenderPass();
    }
}
