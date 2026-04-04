#include "ShadowPassPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::shadow
{
    ShadowPassPipeline::ShadowPassPipeline(core::Device& device)
        : device(device)
    {
    }

    ShadowPassPipeline::~ShadowPassPipeline()
    {
        cleanup();
    }

    void ShadowPassPipeline::init(vk::DescriptorSetLayout perDrawLayout,
                                   vk::DescriptorSetLayout meshletDataLayout,
                                   vk::DescriptorSetLayout vertexDataLayout,
                                   vk::DescriptorSetLayout boneMatrixLayout,
                                   vk::Format atlasDepthFormat)
    {
        if (initialized)
        {
            return;
        }

        cachedPerDrawLayout = perDrawLayout;
        cachedMeshletDataLayout = meshletDataLayout;
        cachedVertexDataLayout = vertexDataLayout;
        cachedBoneMatrixLayout = boneMatrixLayout;
        depthFormat = atlasDepthFormat;

        createCameraDescriptorResources();
        createShadowPipeline();

        initialized = true;
    }

    void ShadowPassPipeline::cleanup()
    {
        if (!initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (shadowShader)
        {
            shadowShader->cleanUp();
            shadowShader.reset();
        }

        if (shadowPipeline)
        {
            vkDevice.destroyPipeline(shadowPipeline);
            shadowPipeline = nullptr;
        }

        if (shadowPipelineLayout)
        {
            vkDevice.destroyPipelineLayout(shadowPipelineLayout);
            shadowPipelineLayout = nullptr;
        }

        if (cameraDescriptorPool)
        {
            vkDevice.destroyDescriptorPool(cameraDescriptorPool);
            cameraDescriptorPool = nullptr;
            cameraDescriptorSet = nullptr;
        }

        if (cameraUBOLayout)
        {
            vkDevice.destroyDescriptorSetLayout(cameraUBOLayout);
            cameraUBOLayout = nullptr;
        }

        initialized = false;
    }

    void ShadowPassPipeline::createShadowPipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shadowShader = std::make_unique<core::Shader>(device);
        shadowShader->readShader("../../resources/shaders/shadow/shadow.glsl");

        const auto& stages = shadowShader->getShaderStages();
        if (stages.size() < 2)
        {
            vfLogError("ShadowPassPipeline: Failed to load shadow shaders: {}",
                        shadowShader->getLastCompilationError());
            return;
        }

        bool hasTask = false, hasMesh = false;
        for (const auto& stage : stages)
        {
            if (stage.stage == vk::ShaderStageFlagBits::eTaskEXT) hasTask = true;
            if (stage.stage == vk::ShaderStageFlagBits::eMeshEXT) hasMesh = true;
        }

        if (!hasTask || !hasMesh)
        {
            vfLogError("ShadowPassPipeline: Missing shader stages (Task={}, Mesh={})", hasTask, hasMesh);
            return;
        }

        std::array<vk::DescriptorSetLayout, 5> setLayouts = {
            cachedPerDrawLayout,
            cachedMeshletDataLayout,
            cachedVertexDataLayout,
            cachedBoneMatrixLayout,
            cameraUBOLayout
        };

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(ShadowPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        shadowPipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        std::array<vk::DynamicState, 3> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
            vk::DynamicState::eDepthBias
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = nullptr;
        viewportState.scissorCount = 1;
        viewportState.pScissors = nullptr;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 0.0f;
        rasterizer.depthBiasSlopeFactor = 0.0f;
        rasterizer.depthBiasClamp = 0.0f;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 0;
        colorBlending.pAttachments = nullptr;

        // Dynamic rendering: depth-only, no color attachments
        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = 0;
        renderingInfo.depthAttachmentFormat = depthFormat;

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = nullptr;
        pipelineInfo.pInputAssemblyState = nullptr;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = shadowPipelineLayout;
        pipelineInfo.renderPass = nullptr;
        pipelineInfo.subpass = 0;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("ShadowPassPipeline: Failed to create pipeline");
            return;
        }

        shadowPipeline = result.value;
    }

    void ShadowPassPipeline::createCameraDescriptorResources()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Create descriptor set layout with a single UBO binding (set 4, binding 0)
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eUniformBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        cameraUBOLayout = core::PipelineUtilities::createUpdateAfterBindLayout(
            vkDevice, &binding, 1);

        // Create descriptor pool
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eUniformBuffer;
        poolSize.descriptorCount = 1;

        cameraDescriptorPool = core::PipelineUtilities::createUpdateAfterBindPool(
            vkDevice, 1, &poolSize, 1);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = cameraDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &cameraUBOLayout;

        cameraDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
    }

    void ShadowPassPipeline::updateCameraDescriptor(vk::Buffer cameraBuffer, vk::DeviceSize bufferSize)
    {
        if (!cameraDescriptorSet)
            return;

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = cameraBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = bufferSize;

        vk::WriteDescriptorSet write{};
        write.dstSet = cameraDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.pBufferInfo = &bufferInfo;

        device.getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
    }
}
