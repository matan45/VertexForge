#include "VegetationMeshShaderPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::vegetation
{
    VegetationMeshShaderPipeline::VegetationMeshShaderPipeline() = default;

    VegetationMeshShaderPipeline::~VegetationMeshShaderPipeline()
    {
        cleanup();
    }

    void VegetationMeshShaderPipeline::init(core::Device& device,
                                             vk::DescriptorSetLayout cameraLayout,
                                             vk::DescriptorSetLayout windLayout,
                                             vk::DescriptorSetLayout meshletDataLayout,
                                             vk::DescriptorSetLayout vertexDataLayout,
                                             vk::DescriptorSetLayout bindlessTextureLayout,
                                             vk::RenderPass renderPass)
    {
        if (initialized) return;

        devicePtr = &device;

        createInstanceDataDescriptor();
        createPipeline(cameraLayout, windLayout, meshletDataLayout, vertexDataLayout, bindlessTextureLayout, renderPass);

        if (graphicsPipeline)
        {
            initialized = true;
            vfLogInfo("VegetationMeshShaderPipeline: Initialized successfully");
        }
        else
        {
            vfLogError("VegetationMeshShaderPipeline: Failed to initialize - pipeline creation failed");
        }
    }

    void VegetationMeshShaderPipeline::cleanup()
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        if (vegShader)
        {
            vegShader->cleanUp();
            vegShader.reset();
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

        initialized = false;
        devicePtr = nullptr;
    }

    void VegetationMeshShaderPipeline::recreate(vk::DescriptorSetLayout cameraLayout,
                                                  vk::DescriptorSetLayout windLayout,
                                                  vk::DescriptorSetLayout meshletDataLayout,
                                                  vk::DescriptorSetLayout vertexDataLayout,
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

        createPipeline(cameraLayout, windLayout, meshletDataLayout, vertexDataLayout, bindlessTextureLayout, renderPass);

        vfLogInfo("VegetationMeshShaderPipeline: Recreated pipeline");
    }

    void VegetationMeshShaderPipeline::updateInstanceDescriptors(vk::Buffer visibleBuffer,
                                                                   vk::Buffer visibleCountBuffer,
                                                                   vk::Buffer treeInstanceBuffer,
                                                                   vk::DeviceSize countBufferOffset,
                                                                   vk::Buffer speciesRenderInfoBuffer)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        uint32_t writeCount = speciesRenderInfoBuffer ? 4u : 3u;

        std::array<vk::DescriptorBufferInfo, 4> bufferInfos{};
        bufferInfos[0].buffer = visibleBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = visibleCountBuffer;
        bufferInfos[1].offset = countBufferOffset;
        bufferInfos[1].range = sizeof(uint32_t);

        bufferInfos[2].buffer = treeInstanceBuffer;
        bufferInfos[2].offset = 0;
        bufferInfos[2].range = VK_WHOLE_SIZE;

        if (speciesRenderInfoBuffer)
        {
            bufferInfos[3].buffer = speciesRenderInfoBuffer;
            bufferInfos[3].offset = 0;
            bufferInfos[3].range = VK_WHOLE_SIZE;
        }

        std::array<vk::WriteDescriptorSet, 4> writes{};
        for (uint32_t i = 0; i < writeCount; ++i)
        {
            writes[i].dstSet = instanceDataDescriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[i].pBufferInfo = &bufferInfos[i];
        }

        vkDevice.updateDescriptorSets(writeCount, writes.data(), 0, nullptr);
    }

    void VegetationMeshShaderPipeline::updateSharedDescriptors(vk::DescriptorSet cameraDescSet,
                                                                 vk::DescriptorSet windDescSet,
                                                                 vk::DescriptorSet meshletDescSet,
                                                                 vk::DescriptorSet vertexDescSet,
                                                                 vk::DescriptorSet bindlessTexDescSet)
    {
        cameraDescriptorSet = cameraDescSet;
        windDescriptorSet = windDescSet;
        meshletDescriptorSet = meshletDescSet;
        vertexDescriptorSet = vertexDescSet;
        bindlessTextureDescriptorSet = bindlessTexDescSet;
    }

    void VegetationMeshShaderPipeline::dispatch(vk::CommandBuffer cmd, uint32_t visibleCount)
    {
        if (!initialized || visibleCount == 0 || !graphicsPipeline) return;

        if (!instanceDataDescriptorSet || !cameraDescriptorSet ||
            !windDescriptorSet || !meshletDescriptorSet || !vertexDescriptorSet)
        {
            return;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        std::array<vk::DescriptorSet, 5> sets = {
            instanceDataDescriptorSet,  // Set 0
            cameraDescriptorSet,        // Set 1
            windDescriptorSet,          // Set 2
            meshletDescriptorSet,       // Set 3
            vertexDescriptorSet         // Set 4
        };
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});

        if (bindlessTextureDescriptorSet)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 5,
                                   {bindlessTextureDescriptorSet}, {});
        }

        uint32_t taskGroups = (visibleCount + 31) / 32;
        cmd.drawMeshTasksEXT(taskGroups, 1, 1);
    }

    void VegetationMeshShaderPipeline::createInstanceDataDescriptor()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        // Set 0: binding 0 = visible instances, binding 1 = visible count,
        //         binding 2 = all tree instances, binding 3 = species render info
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
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

        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        instanceDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 4;

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

    void VegetationMeshShaderPipeline::createPipeline(vk::DescriptorSetLayout cameraLayout,
                                                        vk::DescriptorSetLayout windLayout,
                                                        vk::DescriptorSetLayout meshletDataLayout,
                                                        vk::DescriptorSetLayout vertexDataLayout,
                                                        vk::DescriptorSetLayout bindlessTextureLayout,
                                                        vk::RenderPass renderPass)
    {
        if (!loadShaders()) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        cachedCameraLayout = cameraLayout;
        cachedWindLayout = windLayout;
        cachedMeshletDataLayout = meshletDataLayout;
        cachedVertexDataLayout = vertexDataLayout;
        cachedBindlessTextureLayout = bindlessTextureLayout;

        // 6 descriptor sets matching the shader bindings
        std::array<vk::DescriptorSetLayout, 6> setLayouts = {
            instanceDataLayout,     // Set 0: visible + count + tree instances + species info
            cameraLayout,           // Set 1: camera UBO
            windLayout,             // Set 2: wind UBO
            meshletDataLayout,      // Set 3: meshlet data
            vertexDataLayout,       // Set 4: vertex data
            bindlessTextureLayout   // Set 5: bindless textures
        };

        vk::PipelineLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCreateInfo.pSetLayouts = setLayouts.data();
        layoutCreateInfo.pushConstantRangeCount = 0;

        pipelineLayout = vkDevice.createPipelineLayout(layoutCreateInfo);

        core::MeshShaderPipelineConfig config{};
        config.device = vkDevice;
        config.renderPass = renderPass;
        config.extent = vk::Extent2D{1, 1};
        config.shaderStages = vegShader->getShaderStages();
        config.existingPipelineLayout = pipelineLayout;
        config.cullMode = vk::CullModeFlagBits::eBack;
        config.depthTestEnable = true;
        config.depthWriteEnable = true;
        config.blendEnable = false;
        config.dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};

        try
        {
            auto result = core::PipelineUtilities::createMeshShaderPipeline(config);
            graphicsPipeline = result.pipeline;
        }
        catch (const std::exception& e)
        {
            vfLogError("VegetationMeshShaderPipeline: Failed to create pipeline - {}", e.what());
        }
    }

    bool VegetationMeshShaderPipeline::loadShaders()
    {
        vegShader = std::make_unique<core::Shader>(*devicePtr);
        vegShader->readShader("../../resources/shaders/vegetation/task_vegetation.glsl");
        vegShader->readShader("../../resources/shaders/vegetation/mesh_vegetation.glsl");
        vegShader->readShader("../../resources/shaders/vegetation/frag_vegetation.glsl");

        const auto& stages = vegShader->getShaderStages();
        if (stages.size() < 3)
        {
            vfLogError("VegetationMeshShaderPipeline: Failed to load shaders (need Task + Mesh + Frag): {}",
                        vegShader->getLastCompilationError());
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
            vfLogError("VegetationMeshShaderPipeline: Missing shader stages (Task={}, Mesh={}, Frag={})",
                        hasTask, hasMesh, hasFrag);
            return false;
        }

        return true;
    }
}
