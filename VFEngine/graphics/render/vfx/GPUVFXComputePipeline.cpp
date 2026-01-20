#include "GPUVFXComputePipeline.hpp"
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

        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

        // Binding 0: Particle buffer (storage, read/write)
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 1: Emitter config buffer (storage, read only)
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 2: Emitter state buffer (storage, read/write for atomics)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 3: Draw command buffer (storage, write only)
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void GPUVFXComputePipeline::createPipelineLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Push constants for per-dispatch data
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

        // Load and compile the compute shader
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/vfx/vfx_particle_sim.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            loggerError("GPUVFXComputePipeline: Failed to load shader: {}",
                        shader->getLastCompilationError());
            return;
        }

        // Create compute pipeline
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];  // Single compute stage
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
        poolSizes[0].descriptorCount = 4;  // 4 storage buffers

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

    void GPUVFXComputePipeline::updateDescriptors(
        vk::Buffer particleBuffer,
        vk::Buffer configBuffer,
        vk::Buffer stateBuffer,
        vk::Buffer drawCommandBuffer)
    {
        if (particleBuffer != cachedParticleBuffer ||
            configBuffer != cachedConfigBuffer ||
            stateBuffer != cachedStateBuffer ||
            drawCommandBuffer != cachedDrawCommandBuffer)
        {
            cachedParticleBuffer = particleBuffer;
            cachedConfigBuffer = configBuffer;
            cachedStateBuffer = stateBuffer;
            cachedDrawCommandBuffer = drawCommandBuffer;
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

        // Buffer infos
        vk::DescriptorBufferInfo particleInfo{};
        particleInfo.buffer = cachedParticleBuffer;
        particleInfo.offset = 0;
        particleInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo configInfo{};
        configInfo.buffer = cachedConfigBuffer;
        configInfo.offset = 0;
        configInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo stateInfo{};
        stateInfo.buffer = cachedStateBuffer;
        stateInfo.offset = 0;
        stateInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo drawCmdInfo{};
        drawCmdInfo.buffer = cachedDrawCommandBuffer;
        drawCmdInfo.offset = 0;
        drawCmdInfo.range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 4> writes{};

        // Binding 0: Particle buffer
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &particleInfo;

        // Binding 1: Config buffer
        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &configInfo;

        // Binding 2: State buffer
        writes[2].dstSet = descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].pBufferInfo = &stateInfo;

        // Binding 3: Draw command buffer
        writes[3].dstSet = descriptorSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[3].pBufferInfo = &drawCmdInfo;

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

        // Calculate workgroup count
        uint32_t groupCount = (particleCount + GPUVFXConstants::WORKGROUP_SIZE - 1) /
                              GPUVFXConstants::WORKGROUP_SIZE;

        // Push constants
        GPUVFXComputePushConstants pushConstants{};
        pushConstants.emitterIndex = emitterIndex;
        pushConstants.frameNumber = frameNumber;
        pushConstants.emitterCount = emitterCount;
        pushConstants.totalWorkgroups = groupCount;

        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(GPUVFXComputePushConstants), &pushConstants);

        // Dispatch workgroups
        cmd.dispatch(groupCount, 1, 1);
    }

    void GPUVFXComputePipeline::insertBarriersBeforeCompute(
        vk::CommandBuffer cmd,
        vk::Buffer stateBuffer,
        vk::Buffer drawCommandBuffer,
        vk::Buffer particleBuffer)
    {
        // Barriers: Transfer (fillBuffer/copy) → Compute shader read/write
        std::array<vk::BufferMemoryBarrier, 3> barriers{};

        // State buffer barrier
        barriers[0].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barriers[0].buffer = stateBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        // Draw command buffer barrier
        barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].buffer = drawCommandBuffer;
        barriers[1].offset = 0;
        barriers[1].size = VK_WHOLE_SIZE;

        // Particle buffer barrier (may have been cleared with fillBuffer on first frame)
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

    void GPUVFXComputePipeline::insertBarriersAfterCompute(
        vk::CommandBuffer cmd,
        vk::Buffer particleBuffer,
        vk::Buffer stateBuffer,
        vk::Buffer drawCommandBuffer)
    {
        std::array<vk::BufferMemoryBarrier, 3> barriers{};

        // Barrier 1: Particle buffer
        // Compute shader write → Vertex shader read
        barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[0].buffer = particleBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        // Barrier 2: State buffer (for next frame's reset)
        // Compute shader write → Transfer write (for next reset)
        barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[1].buffer = stateBuffer;
        barriers[1].offset = 0;
        barriers[1].size = VK_WHOLE_SIZE;

        // Barrier 3: Draw command buffer
        // Compute shader write → Indirect command read
        barriers[2].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[2].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[2].buffer = drawCommandBuffer;
        barriers[2].offset = 0;
        barriers[2].size = VK_WHOLE_SIZE;

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
}
