#include "SSRPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"

namespace render::ssr
{
    void SSRPipeline::createTracePipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {traceSet0Layout, traceSet1Layout};

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        tracePipelineLayout = dev.createPipelineLayout(layoutInfo);
        tracePipeline = createFullscreenPipeline(tracePipelineLayout, SSR_FORMAT,
                                                  traceExtent, traceShader, false);
    }

    void SSRPipeline::createTemporalPipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {temporalSet0Layout, temporalSet1Layout};

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        temporalPipelineLayout = dev.createPipelineLayout(layoutInfo);
        temporalPipeline = createFullscreenPipeline(temporalPipelineLayout, SSR_FORMAT,
                                                     traceExtent, temporalShader, false);
    }

    void SSRPipeline::createDenoisePipeline()
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
        denoisePipeline = createFullscreenPipeline(denoisePipelineLayout, SSR_FORMAT,
                                                    traceExtent, denoiseShader, false);
    }

    void SSRPipeline::createCompositePipeline()
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
        compositeColorFormat = swapChain.getSceneColorFormat();
        compositePipeline = createFullscreenPipeline(compositePipelineLayout, compositeColorFormat,
                                                      currentExtent, compositeShader, true);
    }

    void SSRPipeline::loadShaders()
    {
        traceShader = std::make_shared<core::Shader>(device);
        traceShader->readShader("../../resources/shaders/ssr/ssr_trace.glsl");

        temporalShader = std::make_shared<core::Shader>(device);
        temporalShader->readShader("../../resources/shaders/ssr/ssr_temporal.glsl");

        denoiseShader = std::make_shared<core::Shader>(device);
        denoiseShader->readShader("../../resources/shaders/ssr/ssr_denoise.glsl");

        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/ssr/ssr_composite.glsl");
    }

    vk::Pipeline SSRPipeline::createFullscreenPipeline(vk::PipelineLayout layout,
                                                        vk::Format colorFormat,
                                                        vk::Extent2D extent,
                                                        const std::shared_ptr<core::Shader>& shdr,
                                                        bool additiveBlend)
    {
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.extent = extent;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        if (additiveBlend)
        {
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eZero;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        }
        else
        {
            colorBlendAttachment.blendEnable = VK_FALSE;
        }
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR
                                            | vk::ColorComponentFlagBits::eG
                                            | vk::ColorComponentFlagBits::eB
                                            | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        const auto& stages = shdr->getShaderStages();

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = layout;
        pipelineInfo.subpass = 0;

        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        pipelineInfo.pNext = &renderingInfo;

        return device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

}
