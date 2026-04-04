#include "BillboardMeshShaderPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::gpudriven
{
    BillboardMeshShaderPipeline::BillboardMeshShaderPipeline() = default;

    BillboardMeshShaderPipeline::~BillboardMeshShaderPipeline()
    {
        cleanup();
    }

    void BillboardMeshShaderPipeline::init(core::Device& device,
                                            vk::DescriptorSetLayout /*cameraLayout - unused, we create our own*/,
                                            vk::DescriptorSetLayout bindlessTextureLayout,
                                            const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
    {
        if (initialized) return;

        devicePtr = &device;

        createOwnedDescriptors();
        createPipeline(cameraLayout, bindlessTextureLayout, colorFormats, depthFormat);

        if (graphicsPipeline)
        {
            initialized = true;
            vfLogInfo("BillboardMeshShaderPipeline: Initialized successfully");
        }
        else
        {
            vfLogError("BillboardMeshShaderPipeline: Failed to initialize - pipeline creation failed");
        }
    }

    void BillboardMeshShaderPipeline::cleanup()
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        if (taskShader)
        {
            taskShader->cleanUp();
            taskShader.reset();
        }

        if (meshFragShader)
        {
            meshFragShader->cleanUp();
            meshFragShader.reset();
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

        if (cameraPool)
        {
            vkDevice.destroyDescriptorPool(cameraPool);
            cameraPool = nullptr;
        }

        if (cameraLayout)
        {
            vkDevice.destroyDescriptorSetLayout(cameraLayout);
            cameraLayout = nullptr;
        }

        initialized = false;
        devicePtr = nullptr;
    }

    void BillboardMeshShaderPipeline::recreate(vk::DescriptorSetLayout /*cameraLayoutParam*/,
                                                vk::DescriptorSetLayout bindlessTextureLayout,
                                                const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();
        vkDevice.waitIdle();

        if (taskShader)
        {
            taskShader->cleanUp();
            taskShader.reset();
        }

        if (meshFragShader)
        {
            meshFragShader->cleanUp();
            meshFragShader.reset();
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

        createPipeline(cameraLayout, bindlessTextureLayout, colorFormats, depthFormat);

        vfLogInfo("BillboardMeshShaderPipeline: Recreated pipeline");
    }

    void BillboardMeshShaderPipeline::updateInstanceDescriptors(vk::Buffer instanceBuffer,
                                                                  vk::Buffer countBuffer)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorBufferInfo, 2> bufferInfos{};
        bufferInfos[0].buffer = instanceBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = countBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 2> writes{};
        for (uint32_t i = 0; i < 2; ++i)
        {
            writes[i].dstSet = instanceDataDescriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[i].pBufferInfo = &bufferInfos[i];
        }

        vkDevice.updateDescriptorSets(writes, {});
    }

    void BillboardMeshShaderPipeline::updateCameraDescriptor(vk::Buffer cameraBuffer)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = cameraBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet write{};
        write.dstSet = cameraDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.pBufferInfo = &bufferInfo;

        vkDevice.updateDescriptorSets(1, &write, 0, nullptr);
    }

    void BillboardMeshShaderPipeline::updateSharedDescriptors(vk::DescriptorSet bindlessTextureDescSet)
    {
        bindlessTextureDescriptorSet = bindlessTextureDescSet;
    }

    void BillboardMeshShaderPipeline::dispatch(vk::CommandBuffer cmd, uint32_t instanceCount)
    {
        if (!initialized || instanceCount == 0 || !graphicsPipeline) return;

        if (!instanceDataDescriptorSet || !cameraDescriptorSet || !bindlessTextureDescriptorSet)
        {
            return;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        std::array<vk::DescriptorSet, 3> sets = {
            instanceDataDescriptorSet,
            cameraDescriptorSet,
            bindlessTextureDescriptorSet
        };

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
                               sets, {});

        uint32_t taskGroups = (instanceCount + 31) / 32;
        cmd.drawMeshTasksEXT(taskGroups, 1, 1);
    }

    void BillboardMeshShaderPipeline::createOwnedDescriptors()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        // Set 0: binding 0 = billboard instances (SSBO), binding 1 = count (SSBO)
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

            instanceDataLayout = core::PipelineUtilities::createUpdateAfterBindLayout(vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));

            vk::DescriptorPoolSize poolSize{};
            poolSize.type = vk::DescriptorType::eStorageBuffer;
            poolSize.descriptorCount = 2;

            instanceDataPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, &poolSize, 1);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = instanceDataPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &instanceDataLayout;
            auto sets = vkDevice.allocateDescriptorSets(allocInfo);
            instanceDataDescriptorSet = sets[0];
        }

        // Set 1: binding 0 = camera UBO
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eUniformBuffer;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;

            cameraLayout = core::PipelineUtilities::createUpdateAfterBindLayout(vkDevice, &binding, 1);

            vk::DescriptorPoolSize poolSize{};
            poolSize.type = vk::DescriptorType::eUniformBuffer;
            poolSize.descriptorCount = 1;

            cameraPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, &poolSize, 1);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = cameraPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &cameraLayout;
            auto sets = vkDevice.allocateDescriptorSets(allocInfo);
            cameraDescriptorSet = sets[0];
        }
    }

    void BillboardMeshShaderPipeline::createPipeline(vk::DescriptorSetLayout camLayout,
                                                       vk::DescriptorSetLayout bindlessTextureLayout,
                                                       const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
    {
        if (!loadShaders()) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            instanceDataLayout,
            camLayout,
            bindlessTextureLayout
        };

        vk::PipelineLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCreateInfo.pSetLayouts = setLayouts.data();
        layoutCreateInfo.pushConstantRangeCount = 0;

        pipelineLayout = vkDevice.createPipelineLayout(layoutCreateInfo);

        std::vector<vk::PipelineShaderStageCreateInfo> allStages;
        for (const auto& stage : taskShader->getShaderStages())
            allStages.push_back(stage);
        for (const auto& stage : meshFragShader->getShaderStages())
            allStages.push_back(stage);

        core::MeshShaderPipelineConfig config{};
        config.device = vkDevice;
        config.extent = vk::Extent2D{1, 1};
        config.colorAttachmentFormats = colorFormats;
        config.depthAttachmentFormat = depthFormat;
        config.shaderStages = allStages;
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
            vfLogError("BillboardMeshShaderPipeline: Failed to create pipeline - {}", e.what());
        }
    }

    bool BillboardMeshShaderPipeline::loadShaders()
    {
        taskShader = std::make_unique<core::Shader>(*devicePtr);
        taskShader->readShader("../../resources/shaders/gpudriven/task_billboard.glsl");

        const auto& taskStages = taskShader->getShaderStages();
        if (taskStages.empty())
        {
            vfLogError("BillboardMeshShaderPipeline: Failed to load task shader: {}", taskShader->getLastCompilationError());
            return false;
        }

        meshFragShader = std::make_unique<core::Shader>(*devicePtr);
        meshFragShader->readShader("../../resources/shaders/gpudriven/mesh_billboard.glsl");
        meshFragShader->readShader("../../resources/shaders/gpudriven/frag_billboard.glsl");

        const auto& meshFragStages = meshFragShader->getShaderStages();
        if (meshFragStages.size() < 2)
        {
            vfLogError("BillboardMeshShaderPipeline: Failed to load mesh/fragment shaders: {}",
                        meshFragShader->getLastCompilationError());
            return false;
        }

        return true;
    }
}
