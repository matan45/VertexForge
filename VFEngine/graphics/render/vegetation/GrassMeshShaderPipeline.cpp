#include "GrassMeshShaderPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::vegetation
{
    GrassMeshShaderPipeline::GrassMeshShaderPipeline() = default;

    GrassMeshShaderPipeline::~GrassMeshShaderPipeline()
    {
        cleanup();
    }

    void GrassMeshShaderPipeline::init(core::Device& device,
                                        vk::DescriptorSetLayout cameraLayout,
                                        vk::DescriptorSetLayout windLayout,
                                        vk::RenderPass renderPass)
    {
        if (initialized) return;

        devicePtr = &device;

        createGrassDataDescriptor();
        createGrassPipeline(cameraLayout, windLayout, renderPass);

        if (graphicsPipeline)
        {
            initialized = true;
            vfLogInfo("GrassMeshShaderPipeline: Initialized successfully");
        }
        else
        {
            vfLogError("GrassMeshShaderPipeline: Failed to initialize - pipeline creation failed");
        }
    }

    void GrassMeshShaderPipeline::cleanup()
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        if (grassShader)
        {
            grassShader->cleanUp();
            grassShader.reset();
        }

        if (graphicsPipeline)
        {
            vkDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (grassDataPool)
        {
            vkDevice.destroyDescriptorPool(grassDataPool);
            grassDataPool = nullptr;
        }

        if (grassDataLayout)
        {
            vkDevice.destroyDescriptorSetLayout(grassDataLayout);
            grassDataLayout = nullptr;
        }

        initialized = false;
        devicePtr = nullptr;
    }

    void GrassMeshShaderPipeline::recreate(vk::DescriptorSetLayout cameraLayout,
                                            vk::DescriptorSetLayout windLayout,
                                            vk::RenderPass renderPass)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();
        vkDevice.waitIdle();

        if (grassShader)
        {
            grassShader->cleanUp();
            grassShader.reset();
        }

        if (graphicsPipeline)
        {
            vkDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        createGrassPipeline(cameraLayout, windLayout, renderPass);

        vfLogInfo("GrassMeshShaderPipeline: Recreated pipeline");
    }

    void GrassMeshShaderPipeline::updateGrassDataDescriptors(vk::Buffer grassInstanceBuffer,
                                                              vk::Buffer grassCountBuffer)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorBufferInfo, 2> bufferInfos{};
        bufferInfos[0].buffer = grassInstanceBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = grassCountBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = grassDataDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &bufferInfos[0];

        writes[1].dstSet = grassDataDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &bufferInfos[1];

        vkDevice.updateDescriptorSets(writes, {});
    }

    void GrassMeshShaderPipeline::updateSharedDescriptors(vk::DescriptorSet cameraDescSet,
                                                           vk::DescriptorSet windDescSet)
    {
        if (!initialized) return;

        cameraDescriptorSet = cameraDescSet;
        windDescriptorSet = windDescSet;
    }

    void GrassMeshShaderPipeline::dispatch(vk::CommandBuffer cmd,
                                            uint32_t instanceCount,
                                            float fadeStartDistance,
                                            float fadeEndDistance,
                                            const glm::vec4& baseColor,
                                            const glm::vec4& tipColor)
    {
        if (!initialized || instanceCount == 0 || !graphicsPipeline) return;

        if (!grassDataDescriptorSet || !cameraDescriptorSet || !windDescriptorSet)
        {
            return;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        std::array<vk::DescriptorSet, 3> sets = {
            grassDataDescriptorSet,
            cameraDescriptorSet,
            windDescriptorSet
        };
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});

        GrassMeshPushConstants pc{baseColor, tipColor, fadeStartDistance, fadeEndDistance};
        cmd.pushConstants(pipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT |
                          vk::ShaderStageFlagBits::eMeshEXT |
                          vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(GrassMeshPushConstants), &pc);

        uint32_t taskGroups = (instanceCount + 31) / 32;
        cmd.drawMeshTasksEXT(taskGroups, 1, 1);
    }

    void GrassMeshShaderPipeline::createGrassDataDescriptor()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        std::array<vk::DescriptorBindingFlags, 2> bindingFlags;
        bindingFlags.fill(vk::DescriptorBindingFlagBits::eUpdateAfterBind);
        vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        flagsInfo.pBindingFlags = bindingFlags.data();

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        layoutInfo.pNext = &flagsInfo;
        layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
        grassDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
        grassDataPool = vkDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = grassDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &grassDataLayout;
        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        grassDataDescriptorSet = sets[0];
    }

    void GrassMeshShaderPipeline::createGrassPipeline(vk::DescriptorSetLayout cameraLayout,
                                                       vk::DescriptorSetLayout windLayout,
                                                       vk::RenderPass renderPass)
    {
        if (!loadGrassShaders())
        {
            return;
        }

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        this->cameraLayout = cameraLayout;
        this->windLayout = windLayout;

        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            grassDataLayout,
            cameraLayout,
            windLayout
        };

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT |
                               vk::ShaderStageFlagBits::eMeshEXT |
                               vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(GrassMeshPushConstants);

        vk::PipelineLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCreateInfo.pSetLayouts = setLayouts.data();
        layoutCreateInfo.pushConstantRangeCount = 1;
        layoutCreateInfo.pPushConstantRanges = &pushRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutCreateInfo);

        core::MeshShaderPipelineConfig config{};
        config.device = vkDevice;
        config.renderPass = renderPass;
        config.extent = vk::Extent2D{1, 1};
        config.shaderStages = grassShader->getShaderStages();
        config.existingPipelineLayout = pipelineLayout;
        config.cullMode = vk::CullModeFlagBits::eNone;
        config.depthTestEnable = true;
        config.depthWriteEnable = true;
        config.blendEnable = true;
        config.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        config.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        config.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        config.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        config.dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};

        try
        {
            auto result = core::PipelineUtilities::createMeshShaderPipeline(config);
            graphicsPipeline = result.pipeline;
        }
        catch (const std::exception& e)
        {
            vfLogError("GrassMeshShaderPipeline: Failed to create pipeline - {}", e.what());
        }
    }

    bool GrassMeshShaderPipeline::loadGrassShaders()
    {
        grassShader = std::make_unique<core::Shader>(*devicePtr);
        grassShader->readShader("../../resources/shaders/vegetation/task_grass.glsl");
        grassShader->readShader("../../resources/shaders/vegetation/mesh_grass.glsl");
        grassShader->readShader("../../resources/shaders/vegetation/frag_grass.glsl");

        const auto& stages = grassShader->getShaderStages();
        if (stages.size() < 3)
        {
            vfLogError("GrassMeshShaderPipeline: Failed to load shaders (need Task + Mesh + Fragment): {}",
                        grassShader->getLastCompilationError());
            return false;
        }

        bool hasTask = false, hasMesh = false, hasFrag = false;
        for (const auto& stage : stages)
        {
            if (stage.stage == vk::ShaderStageFlagBits::eTaskEXT) hasTask = true;
            if (stage.stage == vk::ShaderStageFlagBits::eMeshEXT) hasMesh = true;
            if (stage.stage == vk::ShaderStageFlagBits::eFragment) hasFrag = true;
        }

        if (!hasTask || !hasMesh || !hasFrag)
        {
            vfLogError("GrassMeshShaderPipeline: Missing shader stages (Task={}, Mesh={}, Fragment={})",
                        hasTask, hasMesh, hasFrag);
            return false;
        }

        return true;
    }
}
