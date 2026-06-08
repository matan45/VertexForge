#include "GIDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/VulkanContext.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Log.hpp"

namespace render::gi
{
    GIDebugRenderer::GIDebugRenderer(core::Device& device)
        : device(device)
    {
    }

    GIDebugRenderer::~GIDebugRenderer()
    {
        cleanup();
    }

    void GIDebugRenderer::init(vk::Format colorFormat, vk::Format depthFormat,
                                vk::DescriptorSetLayout probeDataLayout,
                                vk::DescriptorSetLayout cascadeInfoLayout)
    {
        if (initialized)
        {
            return;
        }

        loadShaders();
        createPipelineLayout(probeDataLayout, cascadeInfoLayout);
        createPipeline(colorFormat, depthFormat);

        initialized = true;
        vfLogInfo("GIDebugRenderer: Initialized");
    }

    void GIDebugRenderer::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (probeDebugPipeline)
        {
            vkDevice.destroyPipeline(probeDebugPipeline);
            probeDebugPipeline = nullptr;
        }
        if (probeDebugPipelineLayout)
        {
            vkDevice.destroyPipelineLayout(probeDebugPipelineLayout);
            probeDebugPipelineLayout = nullptr;
        }

        probeDebugShader.reset();
        initialized = false;
    }

    void GIDebugRenderer::loadShaders()
    {
        probeDebugShader = std::make_unique<core::Shader>(device);
        probeDebugShader->readShader("../../resources/shaders/gi/gi_debug_probe.glsl");
    }

    void GIDebugRenderer::createPipelineLayout(vk::DescriptorSetLayout probeDataLayout,
                                                 vk::DescriptorSetLayout cascadeInfoLayout)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {probeDataLayout, cascadeInfoLayout};

        struct DebugPushConstants
        {
            glm::mat4 viewProjection;
            uint32_t totalProbes;
            uint32_t showMode; // 0=irradiance, 1=validity
            float probeSize;
            float padding;
        };

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(DebugPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        probeDebugPipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
    }

    void GIDebugRenderer::createPipeline(vk::Format colorFormat, vk::Format depthFormat)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        const auto& stages = probeDebugShader->getShaderStages();
        if (stages.empty())
        {
            vfLogWarning("GIDebugRenderer: No shader stages available");
            return;
        }

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::ePointList;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::DynamicState dynamicStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eRasterizationSamplesEXT};
        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = 3;
        dynamicState.pDynamicStates = dynamicStates;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;

        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        renderingInfo.depthAttachmentFormat = depthFormat;

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = probeDebugPipelineLayout;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        probeDebugPipeline = result.value;
    }

    void GIDebugRenderer::render(vk::CommandBuffer cmd,
                                  vk::DescriptorSet probeDataDescSet,
                                  vk::DescriptorSet cascadeInfoDescSet,
                                  const glm::mat4& viewProjection,
                                  uint32_t totalProbes)
    {
        if (!initialized || !hasAnythingToRender() || totalProbes == 0)
        {
            return;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, probeDebugPipeline);

        std::array<vk::DescriptorSet, 2> descSets = {probeDataDescSet, cascadeInfoDescSet};
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, probeDebugPipelineLayout,
                               0, static_cast<uint32_t>(descSets.size()),
                               descSets.data(), 0, nullptr);

        struct DebugPushConstants
        {
            glm::mat4 viewProjection;
            uint32_t totalProbes;
            uint32_t showMode;
            float probeSize;
            float padding;
        } push;

        push.viewProjection = viewProjection;
        push.totalProbes = totalProbes;
        push.showMode = showProbeValidity ? 1 : 0;
        push.probeSize = 1.0f;
        push.padding = 0.0f;

        cmd.pushConstants(probeDebugPipelineLayout,
                          vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(DebugPushConstants), &push);

        // Draw one point per probe
        cmd.draw(totalProbes, 1, 0, 0);
    }
}
