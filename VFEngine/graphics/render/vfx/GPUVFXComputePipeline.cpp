#include "GPUVFXComputePipeline.hpp"
#include "GPUVFXTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Logger.hpp"

namespace render::vfx
{
    GPUVFXComputePipeline::GPUVFXComputePipeline(core::Device& device)
        : device(device)
    {
    }

    GPUVFXComputePipeline::~GPUVFXComputePipeline()
    {
        cleanup();
    }

    void GPUVFXComputePipeline::init()
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
        loggerInfo("GPUVFXComputePipeline initialized");
    }

    void GPUVFXComputePipeline::cleanup()
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
        loggerInfo("GPUVFXComputePipeline cleaned up");
    }

    void GPUVFXComputePipeline::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 7> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[5].binding = 5;
        bindings[5].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[6].binding = 6;
        bindings[6].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void GPUVFXComputePipeline::createPipelineLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(GPUVFXComputePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
    }

    void GPUVFXComputePipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/vfx/vfx_particle_sim.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            loggerError("GPUVFXComputePipeline: Failed to load shader: {}",
                        shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0]; // Single compute stage
        pipelineInfo.layout = pipelineLayout;

        auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            loggerError("GPUVFXComputePipeline: Failed to create compute pipeline");
            return;
        }

        computePipeline = result.value;
    }

    void GPUVFXComputePipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 7;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void GPUVFXComputePipeline::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        auto result = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = result[0];
    }

    void GPUVFXComputePipeline::updateDescriptors(const GPUVFXBufferSet& buffers)
    {
        if (buffers.particleBuffer != cachedBuffers.particleBuffer ||
            buffers.configBuffer != cachedBuffers.configBuffer ||
            buffers.stateBuffer != cachedBuffers.stateBuffer ||
            buffers.drawCommandBuffer != cachedBuffers.drawCommandBuffer ||
            buffers.lutBuffer != cachedBuffers.lutBuffer ||
            buffers.ribbonRingBuffer != cachedBuffers.ribbonRingBuffer ||
            buffers.ribbonHeadBuffer != cachedBuffers.ribbonHeadBuffer)
        {
            cachedBuffers = buffers;
            descriptorsNeedUpdate = true;
        }
    }

    void GPUVFXComputePipeline::writeDescriptors()
    {
        if (!descriptorsNeedUpdate)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo particleInfo{};
        particleInfo.buffer = cachedBuffers.particleBuffer;
        particleInfo.offset = 0;
        particleInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo configInfo{};
        configInfo.buffer = cachedBuffers.configBuffer;
        configInfo.offset = 0;
        configInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo stateInfo{};
        stateInfo.buffer = cachedBuffers.stateBuffer;
        stateInfo.offset = 0;
        stateInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo drawCmdInfo{};
        drawCmdInfo.buffer = cachedBuffers.drawCommandBuffer;
        drawCmdInfo.offset = 0;
        drawCmdInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo lutInfo{};
        lutInfo.buffer = cachedBuffers.lutBuffer;
        lutInfo.offset = 0;
        lutInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo ribbonRingInfo{};
        ribbonRingInfo.buffer = cachedBuffers.ribbonRingBuffer;
        ribbonRingInfo.offset = 0;
        ribbonRingInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo ribbonHeadInfo{};
        ribbonHeadInfo.buffer = cachedBuffers.ribbonHeadBuffer;
        ribbonHeadInfo.offset = 0;
        ribbonHeadInfo.range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 7> writes{};

        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &particleInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &configInfo;

        writes[2].dstSet = descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].pBufferInfo = &stateInfo;

        writes[3].dstSet = descriptorSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[3].pBufferInfo = &drawCmdInfo;

        writes[4].dstSet = descriptorSet;
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[4].pBufferInfo = &lutInfo;

        writes[5].dstSet = descriptorSet;
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[5].pBufferInfo = &ribbonRingInfo;

        writes[6].dstSet = descriptorSet;
        writes[6].dstBinding = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[6].pBufferInfo = &ribbonHeadInfo;

        vkDevice.updateDescriptorSets(writes, {});

        descriptorsNeedUpdate = false;
    }

    void GPUVFXComputePipeline::dispatch(
        vk::CommandBuffer cmd,
        uint32_t emitterIndex,
        uint32_t particleCount,
        uint32_t frameNumber,
        uint32_t emitterCount)
    {
        if (!initialized || particleCount == 0)
        {
            return;
        }

        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, descriptorSet, {});

        uint32_t groupCount = (particleCount + GPUVFXConstants::WORKGROUP_SIZE - 1) /
            GPUVFXConstants::WORKGROUP_SIZE;

        GPUVFXComputePushConstants pushConstants{};
        pushConstants.emitterIndex = emitterIndex;
        pushConstants.frameNumber = frameNumber;
        pushConstants.emitterCount = emitterCount;

        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(GPUVFXComputePushConstants), &pushConstants);

        cmd.dispatch(groupCount, 1, 1);
    }

    void GPUVFXComputePipeline::insertBarriersBeforeCompute(
        vk::CommandBuffer cmd,
        vk::Buffer stateBuffer,
        vk::Buffer drawCommandBuffer,
        vk::Buffer particleBuffer)
    {
        std::array<vk::BufferMemoryBarrier, 3> barriers{};

        barriers[0].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barriers[0].buffer = stateBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].buffer = drawCommandBuffer;
        barriers[1].offset = 0;
        barriers[1].size = VK_WHOLE_SIZE;

        barriers[2].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barriers[2].buffer = particleBuffer;
        barriers[2].offset = 0;
        barriers[2].size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            {},
            barriers,
            {}
        );
    }

    void GPUVFXComputePipeline::insertBarriersAfterCompute(vk::CommandBuffer cmd,
                                                           const GPUVFXBufferSet& buffers)
    {
        std::vector<vk::BufferMemoryBarrier> barriers;
        barriers.reserve(5);

        vk::BufferMemoryBarrier particleBarrier{};
        particleBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        particleBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        particleBarrier.buffer = buffers.particleBuffer;
        particleBarrier.offset = 0;
        particleBarrier.size = VK_WHOLE_SIZE;
        barriers.push_back(particleBarrier);

        vk::BufferMemoryBarrier stateBarrier{};
        stateBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        stateBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        stateBarrier.buffer = buffers.stateBuffer;
        stateBarrier.offset = 0;
        stateBarrier.size = VK_WHOLE_SIZE;
        barriers.push_back(stateBarrier);

        vk::BufferMemoryBarrier drawCmdBarrier{};
        drawCmdBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        drawCmdBarrier.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        drawCmdBarrier.buffer = buffers.drawCommandBuffer;
        drawCmdBarrier.offset = 0;
        drawCmdBarrier.size = VK_WHOLE_SIZE;
        barriers.push_back(drawCmdBarrier);

        if (buffers.ribbonRingBuffer)
        {
            vk::BufferMemoryBarrier ringBarrier{};
            ringBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            ringBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            ringBarrier.buffer = buffers.ribbonRingBuffer;
            ringBarrier.offset = 0;
            ringBarrier.size = VK_WHOLE_SIZE;
            barriers.push_back(ringBarrier);
        }

        if (buffers.ribbonHeadBuffer)
        {
            vk::BufferMemoryBarrier headBarrier{};
            headBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            headBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            headBarrier.buffer = buffers.ribbonHeadBuffer;
            headBarrier.offset = 0;
            headBarrier.size = VK_WHOLE_SIZE;
            barriers.push_back(headBarrier);
        }

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eDrawIndirect |
            vk::PipelineStageFlagBits::eVertexShader,
            {},
            {},
            barriers,
            {}
        );
    }

    void GPUVFXComputePipeline::insertBarriersBeforeTransfer(
        vk::CommandBuffer cmd,
        vk::Buffer stateBuffer,
        vk::Buffer drawCommandBuffer,
        vk::Buffer particleBuffer)
    {
        std::array<vk::BufferMemoryBarrier, 3> barriers{};

        barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[0].buffer = stateBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[1].buffer = drawCommandBuffer;
        barriers[1].offset = 0;
        barriers[1].size = VK_WHOLE_SIZE;

        barriers[2].srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barriers[2].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[2].buffer = particleBuffer;
        barriers[2].offset = 0;
        barriers[2].size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer,
            {},
            {},
            barriers,
            {}
        );
    }

    void GPUVFXComputePipeline::insertTransferToTransferBarrier(
        vk::CommandBuffer cmd,
        vk::Buffer stateBuffer)
    {
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.buffer = stateBuffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer,
            {},
            {},
            {barrier},
            {}
        );
    }
}
