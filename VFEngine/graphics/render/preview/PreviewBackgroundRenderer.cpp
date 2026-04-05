#include "PreviewBackgroundRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/ImageUtilities.hpp"

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
        createPipeline();
        initialized = true;
    }

    void PreviewBackgroundRenderer::cleanUp()
    {
        if (!initialized) return;

        device.getLogicalDevice().destroyPipeline(pipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

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

        vk::Format colorFormat = swapChain.getSceneColorFormat();
        vk::PipelineRenderingCreateInfo pipelineRenderingInfo{};
        pipelineRenderingInfo.colorAttachmentCount = 1;
        pipelineRenderingInfo.pColorAttachmentFormats = &colorFormat;
        pipelineInfo.pNext = &pipelineRenderingInfo;

        auto result = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        pipeline = result.value;
    }

    void PreviewBackgroundRenderer::render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                            const glm::vec4& topColor, const glm::vec4& bottomColor) const
    {
        if (!initialized) return;

        vk::Image colorImage = offscreenResources.colorImages[imageIndex].colorImage;
        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;

        core::ImageUtilities::transitionImageLayout(commandBuffer, colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::DynamicRenderingInfo renderingInfo{};
        renderingInfo.extent = swapChain.getSwapchainExtent();
        renderingInfo.colorAttachments = {core::colorLoad(colorView)};

        core::beginDynamicRendering(commandBuffer, renderingInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        GradientPushConstants pushConstants{};
        pushConstants.topColor = topColor;
        pushConstants.bottomColor = bottomColor;

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(GradientPushConstants), &pushConstants);

        commandBuffer.draw(3, 1, 0, 0); // Fullscreen triangle

        core::endDynamicRendering(commandBuffer);

        core::ImageUtilities::transitionImageLayout(commandBuffer, colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }
}
