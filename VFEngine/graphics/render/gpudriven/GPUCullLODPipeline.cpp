#include "GPUCullLODPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include <spdlog/spdlog.h>
#include <array>

namespace render::gpudriven {

    GPUCullLODPipeline::GPUCullLODPipeline(core::Device& device)
        : device(device)
    {
    }

    GPUCullLODPipeline::~GPUCullLODPipeline()
    {
        cleanup();
    }

    void GPUCullLODPipeline::init()
    {
        if (initialized) {
            return;
        }

        spdlog::info("GPUCullLODPipeline: Initializing...");

        createDescriptorSetLayout();
        createPipelineLayout();
        createComputePipeline();
        createDescriptorPool();
        allocateDescriptorSet();

        initialized = true;
        spdlog::info("GPUCullLODPipeline: Initialized successfully");
    }

    void GPUCullLODPipeline::cleanup()
    {
        if (!initialized) {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (descriptorPool) {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (computePipeline) {
            vkDevice.destroyPipeline(computePipeline);
            computePipeline = nullptr;
        }

        if (pipelineLayout) {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (descriptorSetLayout) {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        if (shader) {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
        spdlog::info("GPUCullLODPipeline: Cleaned up");
    }

    void GPUCullLODPipeline::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Bindings match gpu_cull_lod.glsl:
        // binding 0: ObjectBuffer (storage, read-only)
        // binding 1: CameraUBO (uniform)
        // binding 2: DrawCommandBuffer (storage, write-only)
        // binding 3: PerDrawDataBuffer (storage, write-only)
        // binding 4: DrawCountBuffer (storage, read-write for atomics)
        // binding 5: Hi-Z pyramid texture (combined image sampler)

        std::array<vk::DescriptorSetLayoutBinding, 6> bindings{};

        // Binding 0: Object buffer (GPUObjectData[])
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 1: Camera UBO (GPUCameraData)
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 2: Draw command buffer (DrawIndexedIndirectCommand[])
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 3: Per-draw data buffer (PerDrawData[])
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 4: Draw count buffer (atomic uint)
        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 5: Hi-Z pyramid texture (for occlusion culling)
        bindings[5].binding = 5;
        bindings[5].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        spdlog::debug("GPUCullLODPipeline: Created descriptor set layout");
    }

    void GPUCullLODPipeline::createPipelineLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
        spdlog::debug("GPUCullLODPipeline: Created pipeline layout");
    }

    void GPUCullLODPipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Load and compile shader
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/gpudriven/gpu_cull_lod.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty()) {
            spdlog::error("GPUCullLODPipeline: Failed to load shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            spdlog::error("GPUCullLODPipeline: Failed to create compute pipeline");
            return;
        }

        computePipeline = result.value;
        spdlog::debug("GPUCullLODPipeline: Created compute pipeline");
    }

    void GPUCullLODPipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};

        // Storage buffers: object, draw commands, per-draw data, draw count (4 total)
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 4;

        // Uniform buffer: camera data (1 total)
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        // Combined image sampler: Hi-Z texture (1 total)
        poolSizes[2].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[2].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
        spdlog::debug("GPUCullLODPipeline: Created descriptor pool");
    }

    void GPUCullLODPipeline::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];

        spdlog::debug("GPUCullLODPipeline: Allocated descriptor set");
    }

    void GPUCullLODPipeline::updateDescriptors(
        vk::Buffer objectBuffer,
        vk::Buffer cameraBuffer,
        vk::Buffer drawCommandBuffer,
        vk::Buffer perDrawDataBuffer,
        vk::Buffer drawCountBuffer)
    {
        // Check if any buffer changed
        if (cachedObjectBuffer == objectBuffer &&
            cachedCameraBuffer == cameraBuffer &&
            cachedDrawCommandBuffer == drawCommandBuffer &&
            cachedPerDrawDataBuffer == perDrawDataBuffer &&
            cachedDrawCountBuffer == drawCountBuffer &&
            !descriptorsNeedUpdate) {
            return;
        }

        // Cache the buffers
        cachedObjectBuffer = objectBuffer;
        cachedCameraBuffer = cameraBuffer;
        cachedDrawCommandBuffer = drawCommandBuffer;
        cachedPerDrawDataBuffer = perDrawDataBuffer;
        cachedDrawCountBuffer = drawCountBuffer;

        descriptorsNeedUpdate = true;
    }

    void GPUCullLODPipeline::updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler)
    {
        // Check if Hi-Z descriptor changed
        if (cachedHiZView == hiZView && cachedHiZSampler == hiZSampler && !hiZDescriptorNeedsUpdate) {
            return;
        }

        cachedHiZView = hiZView;
        cachedHiZSampler = hiZSampler;
        hiZDescriptorNeedsUpdate = true;
    }

    void GPUCullLODPipeline::writeDescriptors()
    {
        if (!descriptorsNeedUpdate && !hiZDescriptorNeedsUpdate) {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        // Object buffer info
        vk::DescriptorBufferInfo objectInfo{};
        objectInfo.buffer = cachedObjectBuffer;
        objectInfo.offset = 0;
        objectInfo.range = VK_WHOLE_SIZE;

        // Camera buffer info
        vk::DescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = cachedCameraBuffer;
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(GPUCameraData);

        // Draw command buffer info
        vk::DescriptorBufferInfo drawCmdInfo{};
        drawCmdInfo.buffer = cachedDrawCommandBuffer;
        drawCmdInfo.offset = 0;
        drawCmdInfo.range = VK_WHOLE_SIZE;

        // Per-draw data buffer info
        vk::DescriptorBufferInfo perDrawInfo{};
        perDrawInfo.buffer = cachedPerDrawDataBuffer;
        perDrawInfo.offset = 0;
        perDrawInfo.range = VK_WHOLE_SIZE;

        // Draw count buffer info (includes drawCount + LOD stats)
        vk::DescriptorBufferInfo drawCountInfo{};
        drawCountInfo.buffer = cachedDrawCountBuffer;
        drawCountInfo.offset = 0;
        drawCountInfo.range = VK_WHOLE_SIZE;  // Full buffer including LOD counters

        // Hi-Z image info (optional, may not be available on first frame)
        vk::DescriptorImageInfo hiZInfo{};
        hiZInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        hiZInfo.imageView = cachedHiZView;
        hiZInfo.sampler = cachedHiZSampler;

        // Determine number of writes based on whether Hi-Z is available
        bool hasHiZ = cachedHiZView && cachedHiZSampler;

        std::vector<vk::WriteDescriptorSet> writes;
        writes.reserve(hasHiZ ? 6 : 5);

        // Binding 0: Object buffer
        vk::WriteDescriptorSet objectWrite{};
        objectWrite.dstSet = descriptorSet;
        objectWrite.dstBinding = 0;
        objectWrite.dstArrayElement = 0;
        objectWrite.descriptorCount = 1;
        objectWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        objectWrite.pBufferInfo = &objectInfo;
        writes.push_back(objectWrite);

        // Binding 1: Camera buffer
        vk::WriteDescriptorSet cameraWrite{};
        cameraWrite.dstSet = descriptorSet;
        cameraWrite.dstBinding = 1;
        cameraWrite.dstArrayElement = 0;
        cameraWrite.descriptorCount = 1;
        cameraWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        cameraWrite.pBufferInfo = &cameraInfo;
        writes.push_back(cameraWrite);

        // Binding 2: Draw command buffer
        vk::WriteDescriptorSet drawCmdWrite{};
        drawCmdWrite.dstSet = descriptorSet;
        drawCmdWrite.dstBinding = 2;
        drawCmdWrite.dstArrayElement = 0;
        drawCmdWrite.descriptorCount = 1;
        drawCmdWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        drawCmdWrite.pBufferInfo = &drawCmdInfo;
        writes.push_back(drawCmdWrite);

        // Binding 3: Per-draw data buffer
        vk::WriteDescriptorSet perDrawWrite{};
        perDrawWrite.dstSet = descriptorSet;
        perDrawWrite.dstBinding = 3;
        perDrawWrite.dstArrayElement = 0;
        perDrawWrite.descriptorCount = 1;
        perDrawWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        perDrawWrite.pBufferInfo = &perDrawInfo;
        writes.push_back(perDrawWrite);

        // Binding 4: Draw count buffer
        vk::WriteDescriptorSet drawCountWrite{};
        drawCountWrite.dstSet = descriptorSet;
        drawCountWrite.dstBinding = 4;
        drawCountWrite.dstArrayElement = 0;
        drawCountWrite.descriptorCount = 1;
        drawCountWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        drawCountWrite.pBufferInfo = &drawCountInfo;
        writes.push_back(drawCountWrite);

        // Binding 5: Hi-Z texture (if available)
        if (hasHiZ) {
            vk::WriteDescriptorSet hiZWrite{};
            hiZWrite.dstSet = descriptorSet;
            hiZWrite.dstBinding = 5;
            hiZWrite.dstArrayElement = 0;
            hiZWrite.descriptorCount = 1;
            hiZWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            hiZWrite.pImageInfo = &hiZInfo;
            writes.push_back(hiZWrite);
        }

        vkDevice.updateDescriptorSets(writes, {});
        descriptorsNeedUpdate = false;
        hiZDescriptorNeedsUpdate = false;

        spdlog::debug("GPUCullLODPipeline: Updated descriptors (Hi-Z: {})", hasHiZ ? "yes" : "no");
    }

    void GPUCullLODPipeline::dispatch(vk::CommandBuffer cmd, uint32_t objectCount)
    {
        if (!initialized || objectCount == 0) {
            return;
        }

        // Ensure descriptors are written before dispatch
        writeDescriptors();

        // Bind pipeline and descriptors
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, {});

        // Calculate workgroup count
        uint32_t groupCount = (objectCount + CULL_WORKGROUP_SIZE - 1) / CULL_WORKGROUP_SIZE;
        cmd.dispatch(groupCount, 1, 1);
    }

}
