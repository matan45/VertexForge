#include "BrushComputePipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

#include <cstring>

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
        createCommandPool();

        // Allocate dummy stamp buffer so binding 2 is always valid
        core::BufferInfoRequest dummyRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(float),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::BufferUtilities::createBuffer(dummyRequest, stampBuffer, stampAllocation, device.getMemoryManager());

        vk::DescriptorBufferInfo stampInfo{};
        stampInfo.buffer = stampBuffer;
        stampInfo.offset = 0;
        stampInfo.range = sizeof(float);

        vk::WriteDescriptorSet stampWrite{};
        stampWrite.dstSet = descriptorSet;
        stampWrite.dstBinding = 2;
        stampWrite.descriptorCount = 1;
        stampWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        stampWrite.pBufferInfo = &stampInfo;

        device.getLogicalDevice().updateDescriptorSets({stampWrite}, {});

        initialized = true;
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

        if (computeCommandPool)
        {
            vkDevice.destroyCommandPool(computeCommandPool);
            computeCommandPool = nullptr;
        }

        destroyHeightBuffers();

        core::BufferUtilities::destroyBuffer(vkDevice, stampBuffer, stampAllocation, device.getMemoryManager());
        hasStampData = false;

        shader.reset();

        initialized = false;
    }

    void BrushComputePipeline::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

        // Binding 0: Input heightmap (readonly)
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 1: Output heightmap (writeonly)
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 2: Stamp heightmap data (readonly)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        descriptorSetLayout = core::PipelineUtilities::createUpdateAfterBindLayout(
            vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));
    }

    void BrushComputePipeline::createPipelineLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(BrushComputePushConstants);

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
        shader->readShader("../../resources/shaders/terrain/brush_compute.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("BrushComputePipeline: Failed to load shader: {}",
                        shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
        if (!computePipeline)
        {
            vfLogError("BrushComputePipeline: Failed to create compute pipeline");
            return;
        }
    }

    void BrushComputePipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 3;

        descriptorPool = core::PipelineUtilities::createUpdateAfterBindPool(
            vkDevice, 1, poolSizes.data(), static_cast<uint32_t>(poolSizes.size()));
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

    void BrushComputePipeline::createCommandPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();

        computeCommandPool = vkDevice.createCommandPool(poolInfo);
    }

    void BrushComputePipeline::ensureBufferCapacity(vk::DeviceSize requiredSize)
    {
        if (currentBufferSize >= requiredSize)
        {
            return;
        }

        destroyHeightBuffers();

        // Device-local input buffer (compute reads from this)
        core::BufferInfoRequest inputRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            requiredSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::BufferUtilities::createBuffer(inputRequest, heightInputBuffer, heightInputAllocation, device.getMemoryManager());

        // Device-local output buffer (compute writes to this)
        core::BufferInfoRequest outputRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            requiredSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::BufferUtilities::createBuffer(outputRequest, heightOutputBuffer, heightOutputAllocation, device.getMemoryManager());

        // Host-visible staging buffer for uploading height data to GPU
        core::BufferInfoRequest uploadRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            requiredSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(uploadRequest, stagingUploadBuffer, stagingUploadAllocation, device.getMemoryManager());

        // Host-visible staging buffer for reading back modified heights
        core::BufferInfoRequest readbackRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            requiredSize,
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(readbackRequest, stagingReadbackBuffer, stagingReadbackAllocation, device.getMemoryManager());

        currentBufferSize = requiredSize;

        // Update descriptor set to point to new buffers
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo inputInfo{};
        inputInfo.buffer = heightInputBuffer;
        inputInfo.offset = 0;
        inputInfo.range = requiredSize;

        vk::DescriptorBufferInfo outputInfo{};
        outputInfo.buffer = heightOutputBuffer;
        outputInfo.offset = 0;
        outputInfo.range = requiredSize;

        std::array<vk::WriteDescriptorSet, 2> writes{};

        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &inputInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &outputInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    void BrushComputePipeline::destroyHeightBuffers()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::BufferUtilities::destroyBuffer(vkDevice, heightInputBuffer, heightInputAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, heightOutputBuffer, heightOutputAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, stagingUploadBuffer, stagingUploadAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, stagingReadbackBuffer, stagingReadbackAllocation, device.getMemoryManager());

        currentBufferSize = 0;
    }

    void BrushComputePipeline::setStampData(const std::vector<float>& heights,
                                           uint32_t width, uint32_t height)
    {
        if (!initialized || heights.empty() || width == 0 || height == 0)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vk::DeviceSize dataSize = heights.size() * sizeof(float);

        // Destroy old stamp buffer
        core::BufferUtilities::destroyBuffer(vkDevice, stampBuffer, stampAllocation, device.getMemoryManager());

        // Use host-visible buffer to avoid queue submit (prevents threading errors)
        core::BufferInfoRequest stampRequest(
            vkDevice,
            device.getPhysicalDevice(),
            dataSize,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(stampRequest, stampBuffer, stampAllocation, device.getMemoryManager());

        std::memcpy(stampAllocation.mappedPtr, heights.data(), dataSize);

        // Update descriptor set binding 2
        vk::DescriptorBufferInfo stampInfo{};
        stampInfo.buffer = stampBuffer;
        stampInfo.offset = 0;
        stampInfo.range = dataSize;

        vk::WriteDescriptorSet stampWrite{};
        stampWrite.dstSet = descriptorSet;
        stampWrite.dstBinding = 2;
        stampWrite.descriptorCount = 1;
        stampWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        stampWrite.pBufferInfo = &stampInfo;

        vkDevice.updateDescriptorSets({stampWrite}, {});

        stampWidth = width;
        stampHeight = height;
        hasStampData = true;
    }

    void BrushComputePipeline::clearStampData()
    {
        stampWidth = 0;
        stampHeight = 0;
        hasStampData = false;
    }

    bool BrushComputePipeline::applyBrush(std::vector<float>& heightData,
                                          const BrushComputePushConstants& constants)
    {
        if (!initialized || !computePipeline || constants.verticesPerSide == 0)
        {
            return false;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vk::DeviceSize dataSize = heightData.size() * sizeof(float);

        ensureBufferCapacity(dataSize);

        {
            void* mapped = stagingUploadAllocation.mappedPtr;
            std::memcpy(mapped, heightData.data(), dataSize);
        }

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = computeCommandPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;

        auto cmdBuffers = vkDevice.allocateCommandBuffers(allocInfo);
        vk::CommandBuffer cmd = cmdBuffers[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        // Copy staging → input buffer
        vk::BufferCopy copyRegion{0, 0, dataSize};
        cmd.copyBuffer(stagingUploadBuffer, heightInputBuffer, copyRegion);

        // Barrier: transfer write → compute shader read
        {
            std::array<vk::BufferMemoryBarrier, 2> barriers{};

            barriers[0].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].buffer = heightInputBuffer;
            barriers[0].offset = 0;
            barriers[0].size = dataSize;

            barriers[1].srcAccessMask = {};
            barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].buffer = heightOutputBuffer;
            barriers[1].offset = 0;
            barriers[1].size = dataSize;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, barriers, {}
            );
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, descriptorSet, {});

        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(BrushComputePushConstants), &constants);

        uint32_t groupsX = (constants.verticesPerSide + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        uint32_t groupsY = (constants.verticesPerSide + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        cmd.dispatch(groupsX, groupsY, 1);

        // Barrier: compute shader write → transfer read
        {
            vk::BufferMemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = heightOutputBuffer;
            barrier.offset = 0;
            barrier.size = dataSize;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eTransfer,
                {}, {}, {barrier}, {}
            );
        }

        // Copy output buffer → readback staging
        cmd.copyBuffer(heightOutputBuffer, stagingReadbackBuffer, copyRegion);

        // Barrier: transfer write → host read
        {
            vk::BufferMemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = stagingReadbackBuffer;
            barrier.offset = 0;
            barrier.size = dataSize;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eHost,
                {}, {}, {barrier}, {}
            );
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
            vfLogError("BrushComputePipeline: Failed to wait for compute fence");
            return false;
        }

        {
            void* mapped = stagingReadbackAllocation.mappedPtr;
            std::memcpy(heightData.data(), mapped, dataSize);
        }

        return true;
    }
}
