#include "GPUDrivenPipeline.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Logger.hpp"
#include <array>

namespace render::gpudriven
{
    GPUDrivenPipeline::GPUDrivenPipeline(core::Device& device, core::SwapChain& swapChain)
        : device(device)
        , swapChain(swapChain)
    {
    }

    GPUDrivenPipeline::~GPUDrivenPipeline()
    {
        cleanup();
    }

    void GPUDrivenPipeline::init(vk::DescriptorSetLayout iblLayout,
                                  vk::DescriptorSetLayout bindlessTextureLayout,
                                  vk::RenderPass renderPass)
    {
        createPerDrawDataDescriptor();
        createGraphicsPipeline(iblLayout, bindlessTextureLayout, renderPass);
    }

    void GPUDrivenPipeline::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (meshShader)
        {
            meshShader->cleanUp();
            meshShader.reset();
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

        if (perDrawDataPool)
        {
            vkDevice.destroyDescriptorPool(perDrawDataPool);
            perDrawDataPool = nullptr;
        }

        if (perDrawDataLayout)
        {
            vkDevice.destroyDescriptorSetLayout(perDrawDataLayout);
            perDrawDataLayout = nullptr;
        }
    }

    void GPUDrivenPipeline::recreate(vk::DescriptorSetLayout iblLayout,
                                      vk::DescriptorSetLayout bindlessTextureLayout,
                                      vk::RenderPass renderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        // Destroy old pipeline and layout (keep descriptor set layout and pool)
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

        if (meshShader)
        {
            meshShader->cleanUp();
        }

        createGraphicsPipeline(iblLayout, bindlessTextureLayout, renderPass);
    }

    void GPUDrivenPipeline::updatePerDrawDescriptor(vk::Buffer perDrawDataBuffer)
    {
        vk::DescriptorBufferInfo perDrawInfo{};
        perDrawInfo.buffer = perDrawDataBuffer;
        perDrawInfo.offset = 0;
        perDrawInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet perDrawWrite{};
        perDrawWrite.dstSet = perDrawDataDescriptorSet;
        perDrawWrite.dstBinding = 0;
        perDrawWrite.dstArrayElement = 0;
        perDrawWrite.descriptorCount = 1;
        perDrawWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        perDrawWrite.pBufferInfo = &perDrawInfo;

        device.getLogicalDevice().updateDescriptorSets(perDrawWrite, {});
    }

    void GPUDrivenPipeline::createPerDrawDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Create descriptor set layout for per-draw data (Set 1)
        vk::DescriptorSetLayoutBinding perDrawBinding{};
        perDrawBinding.binding = 0;
        perDrawBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
        perDrawBinding.descriptorCount = 1;
        perDrawBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &perDrawBinding;

        perDrawDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Create descriptor pool
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        perDrawDataPool = vkDevice.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = perDrawDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &perDrawDataLayout;

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        perDrawDataDescriptorSet = sets[0];

        loggerInfo("GPUDrivenPipeline: Created per-draw data descriptor");
    }

    void GPUDrivenPipeline::createGraphicsPipeline(vk::DescriptorSetLayout iblLayout,
                                                    vk::DescriptorSetLayout bindlessTextureLayout,
                                                    vk::RenderPass renderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Load GPU-driven mesh shader
        meshShader = std::make_unique<core::Shader>(device);
        meshShader->readShader("../../resources/shaders/gpudriven/mesh_gpudriven.glsl");

        const auto& stages = meshShader->getShaderStages();
        if (stages.empty())
        {
            loggerError("GPUDrivenPipeline: Failed to load mesh shader: {}", meshShader->getLastCompilationError());
            return;
        }

        // Create pipeline layout with 3 descriptor sets:
        // Set 0: IBL (camera UBO + irradiance + prefilter + brdfLUT)
        // Set 1: Per-draw data storage buffer
        // Set 2: Bindless textures
        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            iblLayout,
            perDrawDataLayout,
            bindlessTextureLayout
        };

        vk::PipelineLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCreateInfo.pSetLayouts = setLayouts.data();
        layoutCreateInfo.pushConstantRangeCount = 0;
        layoutCreateInfo.pPushConstantRanges = nullptr;

        pipelineLayout = vkDevice.createPipelineLayout(layoutCreateInfo);

        // Vertex input state
        auto bindingDescription = mesh::MeshVertexInput::getBindingDescription();
        auto attributeDescriptions = mesh::MeshVertexInput::getAttributeDescriptions();

        core::GraphicsPipelineConfig config{
            .device = vkDevice,
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = stages,
            .vertexBindings = {bindingDescription},
            .vertexAttributes = {attributeDescriptions.begin(), attributeDescriptions.end()},
            .existingPipelineLayout = pipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthWriteEnable = true
        };

        try
        {
            auto result = core::PipelineUtilities::createGraphicsPipeline(config);
            graphicsPipeline = result.pipeline;
            loggerInfo("GPUDrivenPipeline: Created graphics pipeline");
        }
        catch (const std::exception& e)
        {
            loggerError("GPUDrivenPipeline: Failed to create pipeline - {}", e.what());
        }
    }
}
