#include "GPUVFXComputePipeline.hpp"
#include "GPUVFXTypes.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

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
    }

    void GPUVFXComputePipeline::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        constexpr uint32_t BINDING_COUNT = 12; // VK-1501: +childSpawnBuffer at binding 11
        std::array<vk::DescriptorSetLayoutBinding, BINDING_COUNT> bindings{};

        for (uint32_t i = 0; i < BINDING_COUNT; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
        }

        descriptorSetLayout = core::PipelineUtilities::createUpdateAfterBindLayout(vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));
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
            vfLogError("GPUVFXComputePipeline: Failed to load shader: {}",
                        shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
        if (!computePipeline)
        {
            vfLogError("GPUVFXComputePipeline: Failed to create compute pipeline");
            return;
        }
    }

    void GPUVFXComputePipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 12; // VK-1501

        descriptorPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, &poolSize, 1);
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
            buffers.ribbonHeadBuffer != cachedBuffers.ribbonHeadBuffer ||
            buffers.eventBuffer != cachedBuffers.eventBuffer ||
            buffers.colliderBuffer != cachedBuffers.colliderBuffer ||
            buffers.terrainBuffer != cachedBuffers.terrainBuffer ||
            buffers.spawnRequestBuffer != cachedBuffers.spawnRequestBuffer ||
            buffers.childSpawnBuffer != cachedBuffers.childSpawnBuffer)
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

        // Buffers ordered by binding index: particle(0), config(1), state(2),
        // drawCommand(3), lut(4), ribbonRing(5), ribbonHead(6), event(7),
        // collider(8), terrain(9), spawnRequest(10), childSpawn(11)
        std::array<vk::Buffer, 12> bufferHandles = {
            cachedBuffers.particleBuffer,
            cachedBuffers.configBuffer,
            cachedBuffers.stateBuffer,
            cachedBuffers.drawCommandBuffer,
            cachedBuffers.lutBuffer,
            cachedBuffers.ribbonRingBuffer,
            cachedBuffers.ribbonHeadBuffer,
            cachedBuffers.eventBuffer,
            cachedBuffers.colliderBuffer,
            cachedBuffers.terrainBuffer,
            cachedBuffers.spawnRequestBuffer,
            cachedBuffers.childSpawnBuffer
        };

        std::array<vk::DescriptorBufferInfo, 12> bufferInfos{};
        std::array<vk::WriteDescriptorSet, 12> writes{};

        for (uint32_t i = 0; i < 12; ++i)
        {
            bufferInfos[i].buffer = bufferHandles[i];
            bufferInfos[i].offset = 0;
            bufferInfos[i].range = VK_WHOLE_SIZE;

            writes[i].dstSet = descriptorSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[i].pBufferInfo = &bufferInfos[i];
        }

        vkDevice.updateDescriptorSets(writes, {});

        descriptorsNeedUpdate = false;
    }

    void GPUVFXComputePipeline::dispatch(
        vk::CommandBuffer cmd,
        uint32_t emitterIndex,
        uint32_t particleCount,
        uint32_t frameNumber,
        uint32_t emitterCount,
        uint32_t channelRequestBase,
        uint32_t particlesPerRequest,
        uint32_t gpuChildRegion)
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
        pushConstants.channelRequestBase = channelRequestBase;
        pushConstants.particlesPerRequest = particlesPerRequest;
        pushConstants.gpuChildRegion = gpuChildRegion;

        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(GPUVFXComputePushConstants), &pushConstants);

        cmd.dispatch(groupCount, 1, 1);
    }

    void GPUVFXComputePipeline::insertBarriersBeforeCompute(
        vk::CommandBuffer cmd,
        vk::Buffer stateBuffer,
        vk::Buffer drawCommandBuffer,
        vk::Buffer particleBuffer,
        vk::Buffer eventBuffer)
    {
        std::vector<vk::BufferMemoryBarrier> barriers;
        barriers.reserve(6);

        vk::BufferMemoryBarrier stateBarrier{};
        stateBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        stateBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        stateBarrier.buffer = stateBuffer;
        stateBarrier.offset = 0;
        stateBarrier.size = VK_WHOLE_SIZE;
        barriers.push_back(stateBarrier);

        vk::BufferMemoryBarrier drawCmdBarrier{};
        drawCmdBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        drawCmdBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        drawCmdBarrier.buffer = drawCommandBuffer;
        drawCmdBarrier.offset = 0;
        drawCmdBarrier.size = VK_WHOLE_SIZE;
        barriers.push_back(drawCmdBarrier);

        vk::BufferMemoryBarrier particleBarrier{};
        particleBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        particleBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        particleBarrier.buffer = particleBuffer;
        particleBarrier.offset = 0;
        particleBarrier.size = VK_WHOLE_SIZE;
        barriers.push_back(particleBarrier);

        if (eventBuffer)
        {
            vk::BufferMemoryBarrier eventBarrier{};
            eventBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            eventBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
            eventBarrier.buffer = eventBuffer;
            eventBarrier.offset = 0;
            eventBarrier.size = VK_WHOLE_SIZE;
            barriers.push_back(eventBarrier);
        }

        if (cachedBuffers.spawnRequestBuffer)
        {
            vk::BufferMemoryBarrier spawnRequestBarrier{};
            spawnRequestBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            spawnRequestBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            spawnRequestBarrier.buffer = cachedBuffers.spawnRequestBuffer;
            spawnRequestBarrier.offset = 0;
            spawnRequestBarrier.size = VK_WHOLE_SIZE;
            barriers.push_back(spawnRequestBarrier);
        }

        if (cachedBuffers.childSpawnBuffer)
        {
            // VK-1501: the counter reset (fillBuffer) must be visible before parents atomicAdd this
            // frame's write-half counters; children also read (read-half) so include ShaderRead.
            vk::BufferMemoryBarrier childSpawnBarrier{};
            childSpawnBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            childSpawnBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
            childSpawnBarrier.buffer = cachedBuffers.childSpawnBuffer;
            childSpawnBarrier.offset = 0;
            childSpawnBarrier.size = VK_WHOLE_SIZE;
            barriers.push_back(childSpawnBarrier);
        }

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            {},
            barriers,
            {}
        );
    }

    void GPUVFXComputePipeline::insertChildSpawnComputeBarrier(vk::CommandBuffer cmd)
    {
        // VK-1501: cross-frame RAW. The previous frame's parent appends (ShaderWrite) into what is now
        // the read half must be visible to this frame's child-listener reads (ShaderRead). Consecutive
        // frames can overlap on the GPU, so submission order alone is insufficient -- an explicit
        // compute->compute dependency is required. This runs once before the dispatch loop.
        if (!cachedBuffers.childSpawnBuffer)
        {
            return;
        }

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.buffer = cachedBuffers.childSpawnBuffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            {},
            {barrier},
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

        if (buffers.eventBuffer)
        {
            vk::BufferMemoryBarrier eventBarrier{};
            eventBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            eventBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            eventBarrier.buffer = buffers.eventBuffer;
            eventBarrier.offset = 0;
            eventBarrier.size = VK_WHOLE_SIZE;
            barriers.push_back(eventBarrier);
        }

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eDrawIndirect |
            vk::PipelineStageFlagBits::eVertexShader |
            vk::PipelineStageFlagBits::eTransfer,
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
        std::vector<vk::BufferMemoryBarrier> barriers(3);

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

        if (cachedBuffers.spawnRequestBuffer)
        {
            vk::BufferMemoryBarrier spawnRequestBarrier{};
            spawnRequestBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            spawnRequestBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            spawnRequestBarrier.buffer = cachedBuffers.spawnRequestBuffer;
            spawnRequestBarrier.offset = 0;
            spawnRequestBarrier.size = VK_WHOLE_SIZE;
            barriers.push_back(spawnRequestBarrier);
        }

        if (cachedBuffers.childSpawnBuffer)
        {
            // VK-1501: WAR -- the previous frame's child reads and parent writes must complete before
            // this frame's fillBuffer counter reset (TransferWrite) overwrites the write-half counters.
            vk::BufferMemoryBarrier childSpawnBarrier{};
            childSpawnBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
            childSpawnBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            childSpawnBarrier.buffer = cachedBuffers.childSpawnBuffer;
            childSpawnBarrier.offset = 0;
            childSpawnBarrier.size = VK_WHOLE_SIZE;
            barriers.push_back(childSpawnBarrier);
        }

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
