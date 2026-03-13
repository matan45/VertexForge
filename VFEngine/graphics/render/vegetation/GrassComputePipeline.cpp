#include "GrassComputePipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::vegetation
{
    GrassComputePipeline::GrassComputePipeline() = default;

    GrassComputePipeline::~GrassComputePipeline()
    {
        cleanup();
    }

    void GrassComputePipeline::init(core::Device& device)
    {
        if (initialized) return;

        devicePtr = &device;

        vfLogInfo("GrassComputePipeline: Initializing...");

        createDescriptorSetLayout();
        createPipelineLayout();
        createComputePipeline();
        createDescriptorPool();
        allocateDescriptorSet();

        initialized = true;
    }

    void GrassComputePipeline::cleanup()
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

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
        descriptorsNeedUpdate = true;
        devicePtr = nullptr;

        vfLogInfo("GrassComputePipeline: Cleaned up");
    }

    void GrassComputePipeline::updateDescriptors(
        vk::Buffer densityMapBuffer,
        vk::Buffer heightMapBuffer,
        vk::Buffer holeMaskBuffer,
        vk::Buffer grassInstanceBuffer,
        vk::Buffer counterBuffer)
    {
        if (!initialized) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        vk::DescriptorBufferInfo densityInfo{};
        densityInfo.buffer = densityMapBuffer;
        densityInfo.offset = 0;
        densityInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo heightInfo{};
        heightInfo.buffer = heightMapBuffer;
        heightInfo.offset = 0;
        heightInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo holeInfo{};
        holeInfo.buffer = holeMaskBuffer;
        holeInfo.offset = 0;
        holeInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo instanceInfo{};
        instanceInfo.buffer = grassInstanceBuffer;
        instanceInfo.offset = 0;
        instanceInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo counterInfo{};
        counterInfo.buffer = counterBuffer;
        counterInfo.offset = 0;
        counterInfo.range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 5> writes{};

        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &densityInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &heightInfo;

        writes[2].dstSet = descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].pBufferInfo = &holeInfo;

        writes[3].dstSet = descriptorSet;
        writes[3].dstBinding = 3;
        writes[3].dstArrayElement = 0;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[3].pBufferInfo = &instanceInfo;

        writes[4].dstSet = descriptorSet;
        writes[4].dstBinding = 4;
        writes[4].dstArrayElement = 0;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[4].pBufferInfo = &counterInfo;

        vkDevice.updateDescriptorSets(writes, {});
        descriptorsNeedUpdate = false;
    }

    void GrassComputePipeline::dispatch(vk::CommandBuffer cmd, uint32_t texelCount,
                                         const GrassComputePushConstants& pushConstants)
    {
        if (!initialized || texelCount == 0 || descriptorsNeedUpdate) return;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, {});
        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                          sizeof(GrassComputePushConstants), &pushConstants);

        uint32_t groupCount = (texelCount + 63) / 64;
        cmd.dispatch(groupCount, 1, 1);
    }

    void GrassComputePipeline::createDescriptorSetLayout()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        // 5 storage buffer bindings for grass_placement.comp:
        // binding 0: density map (read-only)
        // binding 1: height map (read-only)
        // binding 2: hole mask (read-only)
        // binding 3: grass instance output (read-write)
        // binding 4: atomic counter (read-write)

        std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};

        for (uint32_t i = 0; i < 5; ++i)
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
        vfLogInfo("GrassComputePipeline: Created descriptor set layout");
    }

    void GrassComputePipeline::createPipelineLayout()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(GrassComputePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
        vfLogInfo("GrassComputePipeline: Created pipeline layout");
    }

    void GrassComputePipeline::createComputePipeline()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        shader = std::make_unique<core::Shader>(*devicePtr);
        shader->readShader("../../resources/shaders/vegetation/grass_placement.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("GrassComputePipeline: Failed to load shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("GrassComputePipeline: Failed to create compute pipeline");
            return;
        }

        computePipeline = result.value;
        vfLogInfo("GrassComputePipeline: Created compute pipeline");
    }

    void GrassComputePipeline::createDescriptorPool()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 5;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
        vfLogInfo("GrassComputePipeline: Created descriptor pool");
    }

    void GrassComputePipeline::allocateDescriptorSet()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];

        vfLogInfo("GrassComputePipeline: Allocated descriptor set");
    }

}
