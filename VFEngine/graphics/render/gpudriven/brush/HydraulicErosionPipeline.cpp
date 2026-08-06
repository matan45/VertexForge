#include "HydraulicErosionPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

#include <array>
#include <cstring>

// Windows defines MemoryBarrier as a macro, which conflicts with vk::MemoryBarrier.
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::gpudriven
{
    namespace
    {
        // Per-cell float counts. terrain and sediment carry both ping-pong halves; flux and flow
        // are vec4 (std430 array stride 16 B).
        constexpr uint32_t HEIGHT_FLOATS_PER_CELL = 1;
        constexpr uint32_t TERRAIN_FLOATS_PER_CELL = 2;
        constexpr uint32_t WATER_FLOATS_PER_CELL = 1;
        constexpr uint32_t SEDIMENT_FLOATS_PER_CELL = 2;
        constexpr uint32_t FLUX_FLOATS_PER_CELL = 4;
        constexpr uint32_t FLOW_FLOATS_PER_CELL = 4;

        vk::DeviceSize floatBytes(uint32_t cellCount, uint32_t perCell)
        {
            return static_cast<vk::DeviceSize>(cellCount) * perCell * sizeof(float);
        }
    }

    HydraulicErosionPipeline::HydraulicErosionPipeline(core::Device& device)
        : device(device)
    {
    }

    HydraulicErosionPipeline::~HydraulicErosionPipeline()
    {
        cleanup();
    }

    void HydraulicErosionPipeline::init()
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
        createCommandPool();

        initialized = true;
    }

    void HydraulicErosionPipeline::cleanup()
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

        if (computeCommandPool)
        {
            vkDevice.destroyCommandPool(computeCommandPool);
            computeCommandPool = nullptr;
        }

        destroyBuffers();

        shader.reset();

        initialized = false;
    }

    void HydraulicErosionPipeline::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, BINDING_COUNT> bindings{};
        for (uint32_t i = 0; i < BINDING_COUNT; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
        }

        descriptorSetLayout = core::PipelineUtilities::createUpdateAfterBindLayout(
            vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));
    }

    void HydraulicErosionPipeline::createPipelineLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(HydraulicErosionPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
    }

    void HydraulicErosionPipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/terrain/hydraulic_erosion.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("HydraulicErosionPipeline: Failed to load shader: {}",
                       shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
        if (!computePipeline)
        {
            vfLogError("HydraulicErosionPipeline: Failed to create compute pipeline");
        }
    }

    void HydraulicErosionPipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = BINDING_COUNT;

        descriptorPool = core::PipelineUtilities::createUpdateAfterBindPool(
            vkDevice, 1, poolSizes.data(), static_cast<uint32_t>(poolSizes.size()));
    }

    void HydraulicErosionPipeline::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        auto result = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = result[0];
    }

    void HydraulicErosionPipeline::createCommandPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();

        computeCommandPool = vkDevice.createCommandPool(poolInfo);
    }

    void HydraulicErosionPipeline::destroyBuffers()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        auto& memoryManager = device.getMemoryManager();

        core::BufferUtilities::destroyBuffer(vkDevice, heightOrigBuffer, heightOrigAllocation, memoryManager);
        core::BufferUtilities::destroyBuffer(vkDevice, heightOutBuffer, heightOutAllocation, memoryManager);
        core::BufferUtilities::destroyBuffer(vkDevice, terrainBuffer, terrainAllocation, memoryManager);
        core::BufferUtilities::destroyBuffer(vkDevice, waterBuffer, waterAllocation, memoryManager);
        core::BufferUtilities::destroyBuffer(vkDevice, sedimentBuffer, sedimentAllocation, memoryManager);
        core::BufferUtilities::destroyBuffer(vkDevice, fluxBuffer, fluxAllocation, memoryManager);
        core::BufferUtilities::destroyBuffer(vkDevice, flowBuffer, flowAllocation, memoryManager);
        core::BufferUtilities::destroyBuffer(vkDevice, validBuffer, validAllocation, memoryManager);

        core::BufferUtilities::destroyBuffer(vkDevice, stagingUploadBuffer, stagingUploadAllocation, memoryManager);
        core::BufferUtilities::destroyBuffer(vkDevice, stagingValidBuffer, stagingValidAllocation, memoryManager);
        core::BufferUtilities::destroyBuffer(vkDevice, stagingReadbackBuffer, stagingReadbackAllocation, memoryManager);

        currentCellCount = 0;
    }

    void HydraulicErosionPipeline::ensureCapacity(uint32_t cellCount)
    {
        if (currentCellCount >= cellCount)
        {
            return;
        }

        destroyBuffers();

        vk::Device vkDevice = device.getLogicalDevice();
        auto& memoryManager = device.getMemoryManager();

        // Every simulation buffer is zeroed with vkCmdFillBuffer at the head of each dab, so they
        // all need eTransferDst. Skipping that zero would be silent corruption rather than a
        // validation error: the K limiter divides by the flux sum, so uninitialised memory becomes
        // an inf that survives every later multiply and lands straight in the terrain's CPU mirror.
        const auto simUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;

        auto makeDeviceBuffer = [&](vk::DeviceSize size, vk::BufferUsageFlags usage,
                                    vk::Buffer& buffer, core::VulkanAllocation& allocation)
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice(), size, usage,
                                            vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::BufferUtilities::createBuffer(request, buffer, allocation, memoryManager);
        };

        auto makeHostBuffer = [&](vk::DeviceSize size, vk::BufferUsageFlags usage,
                                  vk::Buffer& buffer, core::VulkanAllocation& allocation)
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice(), size, usage,
                                            vk::MemoryPropertyFlagBits::eHostVisible |
                                            vk::MemoryPropertyFlagBits::eHostCoherent);
            core::BufferUtilities::createBuffer(request, buffer, allocation, memoryManager);
        };

        const vk::DeviceSize heightBytes = floatBytes(cellCount, HEIGHT_FLOATS_PER_CELL);
        const vk::DeviceSize validBytes = static_cast<vk::DeviceSize>(cellCount) * sizeof(uint32_t);

        makeDeviceBuffer(heightBytes, simUsage, heightOrigBuffer, heightOrigAllocation);
        makeDeviceBuffer(heightBytes, vk::BufferUsageFlagBits::eStorageBuffer |
                         vk::BufferUsageFlagBits::eTransferSrc, heightOutBuffer, heightOutAllocation);
        makeDeviceBuffer(floatBytes(cellCount, TERRAIN_FLOATS_PER_CELL), simUsage, terrainBuffer, terrainAllocation);
        makeDeviceBuffer(floatBytes(cellCount, WATER_FLOATS_PER_CELL), simUsage, waterBuffer, waterAllocation);
        makeDeviceBuffer(floatBytes(cellCount, SEDIMENT_FLOATS_PER_CELL), simUsage, sedimentBuffer, sedimentAllocation);
        makeDeviceBuffer(floatBytes(cellCount, FLUX_FLOATS_PER_CELL), simUsage, fluxBuffer, fluxAllocation);
        makeDeviceBuffer(floatBytes(cellCount, FLOW_FLOATS_PER_CELL), simUsage, flowBuffer, flowAllocation);
        makeDeviceBuffer(validBytes, simUsage, validBuffer, validAllocation);

        makeHostBuffer(heightBytes, vk::BufferUsageFlagBits::eTransferSrc,
                       stagingUploadBuffer, stagingUploadAllocation);
        makeHostBuffer(validBytes, vk::BufferUsageFlagBits::eTransferSrc,
                       stagingValidBuffer, stagingValidAllocation);
        makeHostBuffer(heightBytes, vk::BufferUsageFlagBits::eTransferDst,
                       stagingReadbackBuffer, stagingReadbackAllocation);

        currentCellCount = cellCount;

        const std::array<vk::Buffer, BINDING_COUNT> buffers{
            heightOrigBuffer, heightOutBuffer, terrainBuffer, waterBuffer,
            sedimentBuffer, fluxBuffer, flowBuffer, validBuffer
        };
        const std::array<vk::DeviceSize, BINDING_COUNT> ranges{
            heightBytes,
            heightBytes,
            floatBytes(cellCount, TERRAIN_FLOATS_PER_CELL),
            floatBytes(cellCount, WATER_FLOATS_PER_CELL),
            floatBytes(cellCount, SEDIMENT_FLOATS_PER_CELL),
            floatBytes(cellCount, FLUX_FLOATS_PER_CELL),
            floatBytes(cellCount, FLOW_FLOATS_PER_CELL),
            validBytes
        };

        std::array<vk::DescriptorBufferInfo, BINDING_COUNT> infos{};
        std::array<vk::WriteDescriptorSet, BINDING_COUNT> writes{};
        for (uint32_t i = 0; i < BINDING_COUNT; ++i)
        {
            infos[i].buffer = buffers[i];
            infos[i].offset = 0;
            infos[i].range = ranges[i];

            writes[i].dstSet = descriptorSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[i].pBufferInfo = &infos[i];
        }

        vkDevice.updateDescriptorSets(writes, {});
    }

    void HydraulicErosionPipeline::insertStateBarrier(vk::CommandBuffer cmd) const
    {
        // Global rather than per-buffer: every pass writes at least one state buffer and reads
        // another, so a single compute->compute memory barrier is both correct and cheaper than
        // eight buffer barriers per dispatch. Same reasoning as WaterRippleSim::insertStateBarrier.
        vk::MemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                            vk::PipelineStageFlagBits::eComputeShader,
                            {}, barrier, nullptr, nullptr);
    }

    bool HydraulicErosionPipeline::simulate(std::vector<float>& field,
                                            const std::vector<uint32_t>& validMask,
                                            HydraulicErosionPushConstants constants,
                                            uint32_t iterations,
                                            uint32_t thermalInterval)
    {
        const uint32_t cellCount = constants.cellCount;
        if (!initialized || !computePipeline || cellCount == 0 ||
            constants.regionWidth == 0 || constants.regionHeight == 0)
        {
            return false;
        }
        if (field.size() != cellCount || validMask.size() != cellCount)
        {
            vfLogError("HydraulicErosionPipeline: field/mask size mismatch ({}, {} vs {} cells)",
                       field.size(), validMask.size(), cellCount);
            return false;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        ensureCapacity(cellCount);

        const vk::DeviceSize heightBytes = floatBytes(cellCount, HEIGHT_FLOATS_PER_CELL);
        const vk::DeviceSize validBytes = static_cast<vk::DeviceSize>(cellCount) * sizeof(uint32_t);

        std::memcpy(stagingUploadAllocation.mappedPtr, field.data(), heightBytes);
        std::memcpy(stagingValidAllocation.mappedPtr, validMask.data(), validBytes);

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = computeCommandPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;

        auto cmdBuffers = vkDevice.allocateCommandBuffers(allocInfo);
        vk::CommandBuffer cmd = cmdBuffers[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        // Zero the transient state. Water, sediment, flux and flow must start clean every dab --
        // this is the sim's whole memory, and stale flux is what turns into NaN (see ensureCapacity).
        cmd.fillBuffer(waterBuffer, 0, VK_WHOLE_SIZE, 0);
        cmd.fillBuffer(sedimentBuffer, 0, VK_WHOLE_SIZE, 0);
        cmd.fillBuffer(fluxBuffer, 0, VK_WHOLE_SIZE, 0);
        cmd.fillBuffer(flowBuffer, 0, VK_WHOLE_SIZE, 0);
        cmd.fillBuffer(terrainBuffer, 0, VK_WHOLE_SIZE, 0);

        // The gathered heights seed both the untouched reference and the working terrain's slot 0.
        const vk::BufferCopy heightCopy{0, 0, heightBytes};
        cmd.copyBuffer(stagingUploadBuffer, heightOrigBuffer, heightCopy);
        cmd.copyBuffer(stagingUploadBuffer, terrainBuffer, heightCopy);
        cmd.copyBuffer(stagingValidBuffer, validBuffer, vk::BufferCopy{0, 0, validBytes});

        {
            vk::MemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                vk::PipelineStageFlagBits::eComputeShader,
                                {}, barrier, nullptr, nullptr);
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, {});

        const uint32_t groupsX = (constants.regionWidth + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        const uint32_t groupsY = (constants.regionHeight + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

        constants.terrainParity = 0;
        constants.sedimentParity = 0;

        auto dispatchPass = [&](uint32_t passIndex)
        {
            constants.passIndex = passIndex;
            cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                              sizeof(HydraulicErosionPushConstants), &constants);
            cmd.dispatch(groupsX, groupsY, 1);
            insertStateBarrier(cmd);
        };

        const bool thermalEnabled = constants.smoothing > 0.0f && thermalInterval > 0;

        for (uint32_t i = 0; i < iterations; ++i)
        {
            dispatchPass(0); // FLUX
            dispatchPass(1); // WATER
            dispatchPass(2); // EROSION - writes in place, no parity change
            dispatchPass(3); // ADVECT  - writes the other sediment half
            constants.sedimentParity ^= 1u;

            if (thermalEnabled && ((i + 1) % thermalInterval) == 0)
            {
                dispatchPass(4); // THERMAL - writes the other terrain half
                constants.terrainParity ^= 1u;
            }
        }

        dispatchPass(5); // RESOLVE

        {
            vk::BufferMemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = heightOutBuffer;
            barrier.offset = 0;
            barrier.size = heightBytes;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                vk::PipelineStageFlagBits::eTransfer,
                                {}, {}, {barrier}, {});
        }

        cmd.copyBuffer(heightOutBuffer, stagingReadbackBuffer, heightCopy);

        {
            vk::BufferMemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = stagingReadbackBuffer;
            barrier.offset = 0;
            barrier.size = heightBytes;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                vk::PipelineStageFlagBits::eHost,
                                {}, {}, {barrier}, {});
        }

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        vk::Fence fence = vkDevice.createFence({});
        device.submitGraphics(submitInfo, fence);

        auto waitResult = vkDevice.waitForFences(fence, VK_TRUE, UINT64_MAX);
        vkDevice.destroyFence(fence);
        vkDevice.freeCommandBuffers(computeCommandPool, cmd);

        if (waitResult != vk::Result::eSuccess)
        {
            vfLogError("HydraulicErosionPipeline: Failed to wait for compute fence");
            return false;
        }

        std::memcpy(field.data(), stagingReadbackAllocation.mappedPtr, heightBytes);
        return true;
    }
}
