#include "VegetationCullLODPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::vegetation
{
    VegetationCullLODPipeline::VegetationCullLODPipeline() = default;

    VegetationCullLODPipeline::~VegetationCullLODPipeline()
    {
        cleanup();
    }

    void VegetationCullLODPipeline::init(core::Device& device)
    {
        if (initialized) return;

        devicePtr = &device;

        vfLogInfo("VegetationCullLODPipeline: Initializing...");

        createDescriptorSetLayouts();
        createPipelineLayout();
        createComputePipeline();
        createDescriptorPool();
        allocateDescriptorSets();

        initialized = true;
    }

    void VegetationCullLODPipeline::cleanup()
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();
        vkDevice.waitIdle();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (computePipeline)
        {
            vkDevice.destroyPipeline(computePipeline);
            computePipeline = nullptr;
        }

        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (descriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        if (cameraDescriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(cameraDescriptorSetLayout);
            cameraDescriptorSetLayout = nullptr;
        }

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
        descriptorsNeedUpdate = true;
        devicePtr = nullptr;

        vfLogInfo("VegetationCullLODPipeline: Cleaned up");
    }

    void VegetationCullLODPipeline::updateDescriptors(
        vk::Buffer treeInstanceBuffer,
        vk::Buffer instanceCountBuffer,
        vk::Buffer visibleMeshBuffer,
        vk::Buffer countersBuffer)
    {
        if (!initialized) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorBufferInfo, 4> bufferInfos{};

        // binding 0: tree instance buffer (read-only)
        bufferInfos[0].buffer = treeInstanceBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        // binding 1: instance count buffer (read-only)
        bufferInfos[1].buffer = instanceCountBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        // binding 2: visible mesh output (all LODs)
        bufferInfos[2].buffer = visibleMeshBuffer;
        bufferInfos[2].offset = 0;
        bufferInfos[2].range = VK_WHOLE_SIZE;

        // binding 3: atomic counters
        bufferInfos[3].buffer = countersBuffer;
        bufferInfos[3].offset = 0;
        bufferInfos[3].range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 4> writes{};
        for (uint32_t i = 0; i < 4; ++i)
        {
            writes[i].dstSet = descriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[i].pBufferInfo = &bufferInfos[i];
        }

        vkDevice.updateDescriptorSets(writes, {});
        descriptorsNeedUpdate = false;
    }

    void VegetationCullLODPipeline::updateCameraDescriptor(vk::Buffer cameraBuffer)
    {
        if (!initialized) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        vk::DescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = cameraBuffer;
        cameraInfo.offset = 0;
        cameraInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet write{};
        write.dstSet = cameraDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.pBufferInfo = &cameraInfo;

        vkDevice.updateDescriptorSets(1, &write, 0, nullptr);
    }

    void VegetationCullLODPipeline::dispatch(vk::CommandBuffer cmd, uint32_t instanceCount)
    {
        if (!initialized || instanceCount == 0) return;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        std::array<vk::DescriptorSet, 2> sets = {descriptorSet, cameraDescriptorSet};
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, sets, {});

        uint32_t groupCount = (instanceCount + 63) / 64;
        cmd.dispatch(groupCount, 1, 1);
    }

    void VegetationCullLODPipeline::createDescriptorSetLayouts()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        // Set 0: 4 storage buffer bindings (matches vegetation_cull_lod.glsl)
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
        for (uint32_t i = 0; i < 4; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
        }

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Set 1: camera UBO
        vk::DescriptorSetLayoutBinding cameraBinding{};
        cameraBinding.binding = 0;
        cameraBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        cameraBinding.descriptorCount = 1;
        cameraBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo cameraLayoutInfo{};
        cameraLayoutInfo.bindingCount = 1;
        cameraLayoutInfo.pBindings = &cameraBinding;
        cameraDescriptorSetLayout = vkDevice.createDescriptorSetLayout(cameraLayoutInfo);

        vfLogInfo("VegetationCullLODPipeline: Created descriptor set layouts");
    }

    void VegetationCullLODPipeline::createPipelineLayout()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            descriptorSetLayout,
            cameraDescriptorSetLayout
        };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 0;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
        vfLogInfo("VegetationCullLODPipeline: Created pipeline layout");
    }

    void VegetationCullLODPipeline::createComputePipeline()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        shader = std::make_unique<core::Shader>(*devicePtr);
        shader->readShader("../../resources/shaders/vegetation/vegetation_cull_lod.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("VegetationCullLODPipeline: Failed to load shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("VegetationCullLODPipeline: Failed to create compute pipeline");
            return;
        }

        computePipeline = result.value;
        vfLogInfo("VegetationCullLODPipeline: Created compute pipeline");
    }

    void VegetationCullLODPipeline::createDescriptorPool()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 4;
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
        vfLogInfo("VegetationCullLODPipeline: Created descriptor pool");
    }

    void VegetationCullLODPipeline::allocateDescriptorSets()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> layouts = {
            descriptorSetLayout,
            cameraDescriptorSetLayout
        };

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];
        cameraDescriptorSet = sets[1];

        vfLogInfo("VegetationCullLODPipeline: Allocated descriptor sets");
    }
}
