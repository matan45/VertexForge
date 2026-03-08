#include "ImposterPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::vegetation
{
    ImposterPipeline::~ImposterPipeline()
    {
        cleanup();
    }

    void ImposterPipeline::init(core::Device& device,
                                 vk::DescriptorSetLayout cameraLayout,
                                 vk::DescriptorSetLayout bindlessTextureLayout,
                                 vk::RenderPass renderPass)
    {
        if (initialized) return;

        devicePtr = &device;

        createOwnedDescriptors();
        createPipeline(cameraLayout, bindlessTextureLayout, renderPass);

        if (graphicsPipeline)
        {
            initialized = true;
            vfLogInfo("ImposterPipeline: Initialized successfully");
        }
        else
        {
            vfLogError("ImposterPipeline: Failed to initialize - pipeline creation failed");
        }
    }

    void ImposterPipeline::cleanup()
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

        if (imposterConfigPool)
        {
            vkDevice.destroyDescriptorPool(imposterConfigPool);
            imposterConfigPool = nullptr;
        }

        if (imposterConfigLayout)
        {
            vkDevice.destroyDescriptorSetLayout(imposterConfigLayout);
            imposterConfigLayout = nullptr;
        }

        initialized = false;
        devicePtr = nullptr;
    }

    void ImposterPipeline::recreate(vk::DescriptorSetLayout cameraLayout,
                                      vk::DescriptorSetLayout bindlessTextureLayout,
                                      vk::RenderPass renderPass)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();
        vkDevice.waitIdle();

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

        createPipeline(cameraLayout, bindlessTextureLayout, renderPass);

        vfLogInfo("ImposterPipeline: Recreated pipeline");
    }

    void ImposterPipeline::updateInstanceDescriptors(vk::Buffer visibleBuffer,
                                                       vk::Buffer visibleCountBuffer,
                                                       vk::Buffer treeInstanceBuffer)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorBufferInfo, 3> bufferInfos{};
        bufferInfos[0].buffer = visibleBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = visibleCountBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        bufferInfos[2].buffer = treeInstanceBuffer;
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

        vkDevice.updateDescriptorSets(writes, {});
    }

    void ImposterPipeline::updateImposterConfigDescriptor(vk::Buffer imposterConfigBuffer)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = imposterConfigBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet write{};
        write.dstSet = imposterConfigDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.pBufferInfo = &bufferInfo;

        vkDevice.updateDescriptorSets(1, &write, 0, nullptr);
    }

    void ImposterPipeline::updateSharedDescriptors(vk::DescriptorSet cameraDescSet,
                                                     vk::DescriptorSet bindlessTextureDescSet)
    {
        cameraDescriptorSet = cameraDescSet;
        bindlessTextureDescriptorSet = bindlessTextureDescSet;
    }

    void ImposterPipeline::dispatch(vk::CommandBuffer cmd, uint32_t visibleCount)
    {
        if (!initialized || visibleCount == 0 || !graphicsPipeline) return;

        if (!instanceDataDescriptorSet || !cameraDescriptorSet ||
            !imposterConfigDescriptorSet || !bindlessTextureDescriptorSet)
        {
            return;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        std::array<vk::DescriptorSet, 5> sets = {
            instanceDataDescriptorSet,      // Set 0: visible + count + tree instances
            cameraDescriptorSet,            // Set 1: camera UBO
            vk::DescriptorSet{},            // Set 2: unused (placeholder)
            imposterConfigDescriptorSet,    // Set 3: imposter configs
            bindlessTextureDescriptorSet    // Set 4: bindless textures
        };

        // Bind sets 0, 1, 3, 4 (skip set 2)
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
                               {sets[0], sets[1]}, {});
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 3,
                               {sets[3], sets[4]}, {});

        uint32_t taskGroups = (visibleCount + 31) / 32;
        cmd.drawMeshTasksEXT(taskGroups, 1, 1);
    }

    void ImposterPipeline::createOwnedDescriptors()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        // Set 0: binding 0 = visible LOD2 instances, binding 1 = count, binding 2 = all tree instances
        {
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
            bindings[2].stageFlags = vk::ShaderStageFlagBits::eMeshEXT;

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

        // Set 3: binding 0 = imposter config buffer
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eStorageBuffer;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eMeshEXT;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;
            imposterConfigLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

            vk::DescriptorPoolSize poolSize{};
            poolSize.type = vk::DescriptorType::eStorageBuffer;
            poolSize.descriptorCount = 1;

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            imposterConfigPool = vkDevice.createDescriptorPool(poolInfo);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = imposterConfigPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &imposterConfigLayout;
            auto sets = vkDevice.allocateDescriptorSets(allocInfo);
            imposterConfigDescriptorSet = sets[0];
        }
    }

    void ImposterPipeline::createPipeline(vk::DescriptorSetLayout cameraLayout,
                                            vk::DescriptorSetLayout bindlessTextureLayout,
                                            vk::RenderPass renderPass)
    {
        if (!loadShaders()) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        cachedCameraLayout = cameraLayout;
        cachedBindlessTextureLayout = bindlessTextureLayout;

        // Need an empty layout for set 2 (unused gap between set 1 and set 3)
        vk::DescriptorSetLayoutCreateInfo emptyLayoutInfo{};
        emptyLayoutInfo.bindingCount = 0;
        vk::DescriptorSetLayout emptyLayout = vkDevice.createDescriptorSetLayout(emptyLayoutInfo);

        // 5 descriptor sets: 0=instance, 1=camera, 2=empty, 3=imposter config, 4=bindless textures
        std::array<vk::DescriptorSetLayout, 5> setLayouts = {
            instanceDataLayout,
            cameraLayout,
            emptyLayout,
            imposterConfigLayout,
            bindlessTextureLayout
        };

        vk::PipelineLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCreateInfo.pSetLayouts = setLayouts.data();
        layoutCreateInfo.pushConstantRangeCount = 0;

        pipelineLayout = vkDevice.createPipelineLayout(layoutCreateInfo);

        // Destroy the empty layout after pipeline layout creation
        vkDevice.destroyDescriptorSetLayout(emptyLayout);

        // Merge shader stages from task shader and mesh+frag shader
        std::vector<vk::PipelineShaderStageCreateInfo> allStages;
        for (const auto& stage : taskShader->getShaderStages())
            allStages.push_back(stage);
        for (const auto& stage : meshFragShader->getShaderStages())
            allStages.push_back(stage);

        core::MeshShaderPipelineConfig config{};
        config.device = vkDevice;
        config.renderPass = renderPass;
        config.extent = vk::Extent2D{1, 1};
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
            vfLogError("ImposterPipeline: Failed to create pipeline - {}", e.what());
        }
    }

    bool ImposterPipeline::loadShaders()
    {
        // Task shader (separate file, uses subgroup ballot)
        taskShader = std::make_unique<core::Shader>(*devicePtr);
        taskShader->readShader("../../resources/shaders/vegetation/task_imposter.glsl");

        const auto& taskStages = taskShader->getShaderStages();
        if (taskStages.empty())
        {
            vfLogError("ImposterPipeline: Failed to load task shader: {}", taskShader->getLastCompilationError());
            return false;
        }

        // Mesh + fragment shaders
        meshFragShader = std::make_unique<core::Shader>(*devicePtr);
        meshFragShader->readShader("../../resources/shaders/vegetation/mesh_imposter.glsl");
        meshFragShader->readShader("../../resources/shaders/vegetation/frag_imposter.glsl");

        const auto& meshFragStages = meshFragShader->getShaderStages();
        if (meshFragStages.size() < 2)
        {
            vfLogError("ImposterPipeline: Failed to load mesh/fragment shaders: {}",
                        meshFragShader->getLastCompilationError());
            return false;
        }

        return true;
    }
}
