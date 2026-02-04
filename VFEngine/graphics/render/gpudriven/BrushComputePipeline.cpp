#include "BrushComputePipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Logger.hpp"

namespace render::gpudriven
{
    BrushComputePipeline::BrushComputePipeline(core::Device& device)
        : device(device)
    {
    }

    BrushComputePipeline::~BrushComputePipeline()
    {
        cleanup();
    }

    void BrushComputePipeline::init()
    {
        if (initialized)
        {
            return;
        }

        createDescriptorSetLayout();
        createPipelineLayout();
        createComputePipeline();
        createDescriptorPool();
        allocateDescriptorSet();

        initialized = true;
        loggerInfo("BrushComputePipeline initialized");
    }

    void BrushComputePipeline::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

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

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (descriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        shader.reset();

        initialized = false;
        loggerInfo("BrushComputePipeline cleaned up");
    }

    void BrushComputePipeline::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void BrushComputePipeline::createPipelineLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(BrushPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
    }

    void BrushComputePipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/terrain/brush_influence.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            loggerError("BrushComputePipeline: Failed to load shader: {}",
                        shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            loggerError("BrushComputePipeline: Failed to create compute pipeline");
            return;
        }

        computePipeline = result.value;
    }

    void BrushComputePipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void BrushComputePipeline::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        auto result = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = result[0];
    }

    void BrushComputePipeline::updateDescriptors(
        vk::Buffer heightmapBuffer,
        vk::Buffer influenceBuffer)
    {
        if (heightmapBuffer != cachedHeightmapBuffer ||
            influenceBuffer != cachedInfluenceBuffer)
        {
            cachedHeightmapBuffer = heightmapBuffer;
            cachedInfluenceBuffer = influenceBuffer;
            descriptorsNeedUpdate = true;
        }
    }

    void BrushComputePipeline::writeDescriptors()
    {
        if (!descriptorsNeedUpdate)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo heightmapInfo{};
        heightmapInfo.buffer = cachedHeightmapBuffer;
        heightmapInfo.offset = 0;
        heightmapInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo influenceInfo{};
        influenceInfo.buffer = cachedInfluenceBuffer;
        influenceInfo.offset = 0;
        influenceInfo.range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 2> writes{};

        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &heightmapInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &influenceInfo;

        vkDevice.updateDescriptorSets(writes, {});

        descriptorsNeedUpdate = false;
    }

    void BrushComputePipeline::dispatch(
        vk::CommandBuffer cmd,
        const BrushPushConstants& constants)
    {
        if (!initialized || constants.vertexCount == 0)
        {
            return;
        }

        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, descriptorSet, {});

        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(BrushPushConstants), &constants);

        uint32_t groupsX = (constants.vertexCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        uint32_t groupsY = (constants.vertexCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

        cmd.dispatch(groupsX, groupsY, 1);
    }

    void BrushComputePipeline::insertBarriersBeforeCompute(
        vk::CommandBuffer cmd,
        vk::Buffer heightmapBuffer,
        vk::Buffer influenceBuffer)
    {
        std::array<vk::BufferMemoryBarrier, 2> barriers{};

        barriers[0].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[0].buffer = heightmapBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].buffer = influenceBuffer;
        barriers[1].offset = 0;
        barriers[1].size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            {},
            barriers,
            {}
        );
    }

    void BrushComputePipeline::insertBarriersAfterCompute(
        vk::CommandBuffer cmd,
        vk::Buffer influenceBuffer)
    {
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eShaderRead;
        barrier.buffer = influenceBuffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eVertexShader,
            {},
            {},
            {barrier},
            {}
        );
    }
}
