#include "VegetationShadowPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::shadow
{
    VegetationShadowPipeline::VegetationShadowPipeline(core::Device& device)
        : device(device)
    {
    }

    VegetationShadowPipeline::~VegetationShadowPipeline()
    {
        cleanup();
    }

    void VegetationShadowPipeline::init(vk::DescriptorSetLayout meshletDataLayout,
                                          vk::DescriptorSetLayout vertexDataLayout,
                                          vk::RenderPass shadowRenderPass)
    {
        if (initialized) return;

        cachedMeshletDataLayout = meshletDataLayout;
        cachedVertexDataLayout = vertexDataLayout;

        createInstanceDataDescriptor();
        createPipeline(shadowRenderPass);

        initialized = true;
    }

    void VegetationShadowPipeline::cleanup()
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (shadowShader)
        {
            shadowShader->cleanUp();
            shadowShader.reset();
        }

        if (pipeline)
        {
            vkDevice.destroyPipeline(pipeline);
            pipeline = nullptr;
        }

        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (instanceDataPool)
        {
            vkDevice.destroyDescriptorPool(instanceDataPool);
            instanceDataPool = nullptr;
        }

        if (instanceDataLayout)
        {
            vkDevice.destroyDescriptorSetLayout(instanceDataLayout);
            instanceDataLayout = nullptr;
        }

        initialized = false;
    }

    void VegetationShadowPipeline::createInstanceDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Set 0: binding 0 = tree instances, binding 1 = instance count, binding 2 = species render info
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        instanceDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 3;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        instanceDataPool = vkDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = instanceDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &instanceDataLayout;
        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        instanceDataDescriptorSet = sets[0];
    }

    void VegetationShadowPipeline::updateInstanceDescriptors(vk::Buffer treeInstanceBuffer,
                                                               vk::Buffer instanceCountBuffer,
                                                               vk::Buffer speciesRenderInfoBuffer)
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorBufferInfo, 3> bufferInfos{};
        bufferInfos[0].buffer = treeInstanceBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = instanceCountBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = sizeof(uint32_t);

        bufferInfos[2].buffer = speciesRenderInfoBuffer;
        bufferInfos[2].offset = 0;
        bufferInfos[2].range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 3> writes{};
        for (uint32_t i = 0; i < 3; ++i)
        {
            writes[i].dstSet = instanceDataDescriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[i].pBufferInfo = &bufferInfos[i];
        }

        vkDevice.updateDescriptorSets(3, writes.data(), 0, nullptr);
    }

    void VegetationShadowPipeline::createPipeline(vk::RenderPass shadowRenderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shadowShader = std::make_unique<core::Shader>(device);
        shadowShader->readShader("../../resources/shaders/shadow/shadow_vegetation.glsl");

        const auto& stages = shadowShader->getShaderStages();
        if (stages.size() < 2)
        {
            vfLogError("VegetationShadowPipeline: Failed to load shaders: {}",
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
            vfLogError("VegetationShadowPipeline: Missing shader stages (Task={}, Mesh={})", hasTask, hasMesh);
            return;
        }

        // Set 0: instance data (owned)
        // Set 1: meshlet data (shared)
        // Set 2: vertex data (shared)
        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            instanceDataLayout,
            cachedMeshletDataLayout,
            cachedVertexDataLayout
        };

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(VegetationShadowPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

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

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
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
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = shadowRenderPass;
        pipelineInfo.subpass = 0;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("VegetationShadowPipeline: Failed to create pipeline");
            return;
        }

        pipeline = result.value;
        vfLogInfo("VegetationShadowPipeline: Initialized successfully");
    }

    void VegetationShadowPipeline::dispatch(vk::CommandBuffer cmd,
                                              vk::DescriptorSet meshletDescSet,
                                              vk::DescriptorSet vertexDescSet,
                                              const glm::mat4& lightViewProjection,
                                              uint32_t instanceCount,
                                              uint32_t shadowLOD,
                                              float depthBias,
                                              float slopeBias)
    {
        if (!initialized || instanceCount == 0) return;

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        std::array<vk::DescriptorSet, 3> descriptorSets = {
            instanceDataDescriptorSet,
            meshletDescSet,
            vertexDescSet
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipelineLayout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0,
            nullptr
        );

        VegetationShadowPushConstants pushConstants{};
        pushConstants.lightViewProjection = lightViewProjection;
        pushConstants.instanceCount = instanceCount;
        pushConstants.shadowLOD = shadowLOD;
        pushConstants.depthBias = depthBias;
        pushConstants.slopeBias = slopeBias;

        cmd.pushConstants(
            pipelineLayout,
            vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
            0,
            sizeof(VegetationShadowPushConstants),
            &pushConstants
        );

        cmd.setDepthBias(depthBias, 0.0f, slopeBias);

        // One workgroup per 32 instances (task shader local_size_x = 32)
        uint32_t workgroupCount = (instanceCount + 31) / 32;
        cmd.drawMeshTasksEXT(workgroupCount, 1, 1);
    }
}
