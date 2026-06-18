#include "GPUCullLODPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../GPUDrivenTypes.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <array>

namespace render::gpudriven
{
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
        if (initialized)
        {
            return;
        }

        vfLogInfo("GPUCullLODPipeline: Initializing...");

        createDescriptorSetLayout();
        createPipelineLayout();
        createComputePipeline();
        createShadowPipeline();
        createDescriptorPool();
        allocateDescriptorSet();

        initialized = true;
    }

    void GPUCullLODPipeline::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
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

        if (shadowComputePipeline)
        {
            vkDevice.destroyPipeline(shadowComputePipeline);
            shadowComputePipeline = nullptr;
        }

        if (shadowPipelineLayout)
        {
            vkDevice.destroyPipelineLayout(shadowPipelineLayout);
            shadowPipelineLayout = nullptr;
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
        // binding 6: ActiveIndexBuffer (storage, read-only)

        std::array<vk::DescriptorSetLayoutBinding, 7> bindings{};

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

        // Binding 6: Active-index buffer (uint[])
        bindings[6].binding = 6;
        bindings[6].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags = vk::ShaderStageFlagBits::eCompute;

        descriptorSetLayout = core::PipelineUtilities::createUpdateAfterBindLayout(
            vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));
        vfLogInfo("GPUCullLODPipeline: Created descriptor set layout");
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
        vfLogWarning("GPUCullLODPipeline: Created pipeline layout");
    }

    void GPUCullLODPipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/gpudriven/gpu_cull_lod.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("GPUCullLODPipeline: Failed to load shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
        if (!computePipeline)
        {
            vfLogError("GPUCullLODPipeline: Failed to create compute pipeline");
            return;
        }
        vfLogWarning("GPUCullLODPipeline: Created compute pipeline");
    }

    void GPUCullLODPipeline::createShadowPipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Same descriptor-set layout as the main cull pipeline (so external shadow sets are
        // compatible), plus a push-constant range for the per-view base/capacity/countIndex.
        vk::PushConstantRange pcRange{};
        pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pcRange.offset = 0;
        pcRange.size = sizeof(ShadowCullPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pcRange;
        shadowPipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        auto shadowShader = std::make_unique<core::Shader>(device);
        shadowShader->readShader("../../resources/shaders/gpudriven/gpu_cull_shadow.glsl");
        const auto& stages = shadowShader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("GPUCullLODPipeline: Failed to load shadow cull shader: {}",
                       shadowShader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = shadowPipelineLayout;
        shadowComputePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
        if (!shadowComputePipeline)
        {
            vfLogError("GPUCullLODPipeline: Failed to create shadow cull pipeline");
            return;
        }
        // Keep the shader module alive until pipeline creation completes, then release.
        shadowShader->cleanUp();
        vfLogInfo("GPUCullLODPipeline: Created shadow cull pipeline");
    }

    void GPUCullLODPipeline::dispatchShadowWithSet(vk::CommandBuffer cmd, uint32_t objectCount,
                                                    vk::DescriptorSet externalSet,
                                                    const ShadowCullPushConstants& pc)
    {
        if (!initialized || !shadowComputePipeline || objectCount == 0 || !externalSet)
        {
            return;
        }

        // External shadow sets are kept in sync inside writeDescriptors(); flush pending updates.
        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, shadowComputePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, shadowPipelineLayout, 0, externalSet, {});
        cmd.pushConstants(shadowPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                          sizeof(ShadowCullPushConstants), &pc);

        uint32_t groupCount = (objectCount + CULL_WORKGROUP_SIZE - 1) / CULL_WORKGROUP_SIZE;
        cmd.dispatch(groupCount, 1, 1);
    }

    void GPUCullLODPipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};

        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 5;

        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        poolSizes[2].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[2].descriptorCount = 1;

        descriptorPool = core::PipelineUtilities::createUpdateAfterBindPool(
            vkDevice, 1, poolSizes.data(), static_cast<uint32_t>(poolSizes.size()));
        vfLogInfo("GPUCullLODPipeline: Created descriptor pool");
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

        vfLogWarning("GPUCullLODPipeline: Allocated descriptor set");
    }

    void GPUCullLODPipeline::updateDescriptors(
        vk::Buffer objectBuffer,
        vk::Buffer cameraBuffer,
        vk::Buffer drawCommandBuffer,
        vk::Buffer perDrawDataBuffer,
        vk::Buffer drawCountBuffer,
        vk::Buffer activeIndexBuffer)
    {
        // Check if any buffer changed
        if (cachedObjectBuffer == objectBuffer &&
            cachedCameraBuffer == cameraBuffer &&
            cachedDrawCommandBuffer == drawCommandBuffer &&
            cachedPerDrawDataBuffer == perDrawDataBuffer &&
            cachedDrawCountBuffer == drawCountBuffer &&
            cachedActiveIndexBuffer == activeIndexBuffer &&
            !descriptorsNeedUpdate)
        {
            return;
        }

        // Cache the buffers
        cachedObjectBuffer = objectBuffer;
        cachedCameraBuffer = cameraBuffer;
        cachedDrawCommandBuffer = drawCommandBuffer;
        cachedPerDrawDataBuffer = perDrawDataBuffer;
        cachedDrawCountBuffer = drawCountBuffer;
        cachedActiveIndexBuffer = activeIndexBuffer;

        descriptorsNeedUpdate = true;
    }

    void GPUCullLODPipeline::updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler)
    {
        if (cachedHiZView == hiZView && cachedHiZSampler == hiZSampler && !hiZDescriptorNeedsUpdate)
        {
            return;
        }

        cachedHiZView = hiZView;
        cachedHiZSampler = hiZSampler;
        hiZDescriptorNeedsUpdate = true;
    }

    void GPUCullLODPipeline::writeBindingsToSet(const ExternalDescriptorRef& ref)
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vk::DescriptorSet dst = ref.descriptorSet;

        vk::DescriptorBufferInfo objectInfo{};
        objectInfo.buffer = cachedObjectBuffer;
        objectInfo.offset = 0;
        objectInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = ref.cameraBuffer;
        cameraInfo.offset = ref.cameraOffset;
        cameraInfo.range = ref.cameraRange != 0 ? ref.cameraRange : sizeof(GPUCameraData);

        // Output buffers default to the cached main buffers; shadow sets override them.
        vk::DescriptorBufferInfo drawCmdInfo{};
        drawCmdInfo.buffer = ref.drawCommandOverride ? ref.drawCommandOverride : cachedDrawCommandBuffer;
        drawCmdInfo.offset = 0;
        drawCmdInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo perDrawInfo{};
        perDrawInfo.buffer = ref.perDrawDataOverride ? ref.perDrawDataOverride : cachedPerDrawDataBuffer;
        perDrawInfo.offset = 0;
        perDrawInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo drawCountInfo{};
        drawCountInfo.buffer = ref.drawCountOverride ? ref.drawCountOverride : cachedDrawCountBuffer;
        drawCountInfo.offset = 0;
        drawCountInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo activeIndexInfo{};
        activeIndexInfo.buffer = cachedActiveIndexBuffer;
        activeIndexInfo.offset = 0;
        activeIndexInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorImageInfo hiZInfo{};
        hiZInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        hiZInfo.imageView = cachedHiZView;
        hiZInfo.sampler = cachedHiZSampler;

        bool hasHiZ = cachedHiZView && cachedHiZSampler;

        std::vector<vk::WriteDescriptorSet> writes;
        writes.reserve(hasHiZ ? 7 : 6);

        vk::WriteDescriptorSet objectWrite{};
        objectWrite.dstSet = dst;
        objectWrite.dstBinding = 0;
        objectWrite.dstArrayElement = 0;
        objectWrite.descriptorCount = 1;
        objectWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        objectWrite.pBufferInfo = &objectInfo;
        writes.push_back(objectWrite);

        vk::WriteDescriptorSet cameraWrite{};
        cameraWrite.dstSet = dst;
        cameraWrite.dstBinding = 1;
        cameraWrite.dstArrayElement = 0;
        cameraWrite.descriptorCount = 1;
        cameraWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        cameraWrite.pBufferInfo = &cameraInfo;
        writes.push_back(cameraWrite);

        vk::WriteDescriptorSet drawCmdWrite{};
        drawCmdWrite.dstSet = dst;
        drawCmdWrite.dstBinding = 2;
        drawCmdWrite.dstArrayElement = 0;
        drawCmdWrite.descriptorCount = 1;
        drawCmdWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        drawCmdWrite.pBufferInfo = &drawCmdInfo;
        writes.push_back(drawCmdWrite);

        vk::WriteDescriptorSet perDrawWrite{};
        perDrawWrite.dstSet = dst;
        perDrawWrite.dstBinding = 3;
        perDrawWrite.dstArrayElement = 0;
        perDrawWrite.descriptorCount = 1;
        perDrawWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        perDrawWrite.pBufferInfo = &perDrawInfo;
        writes.push_back(perDrawWrite);

        vk::WriteDescriptorSet drawCountWrite{};
        drawCountWrite.dstSet = dst;
        drawCountWrite.dstBinding = 4;
        drawCountWrite.dstArrayElement = 0;
        drawCountWrite.descriptorCount = 1;
        drawCountWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        drawCountWrite.pBufferInfo = &drawCountInfo;
        writes.push_back(drawCountWrite);

        vk::WriteDescriptorSet hiZWrite{};
        if (hasHiZ)
        {
            hiZWrite.dstSet = dst;
            hiZWrite.dstBinding = 5;
            hiZWrite.dstArrayElement = 0;
            hiZWrite.descriptorCount = 1;
            hiZWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            hiZWrite.pImageInfo = &hiZInfo;
            writes.push_back(hiZWrite);
        }

        if (cachedActiveIndexBuffer)
        {
            vk::WriteDescriptorSet activeIndexWrite{};
            activeIndexWrite.dstSet = dst;
            activeIndexWrite.dstBinding = 6;
            activeIndexWrite.dstArrayElement = 0;
            activeIndexWrite.descriptorCount = 1;
            activeIndexWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
            activeIndexWrite.pBufferInfo = &activeIndexInfo;
            writes.push_back(activeIndexWrite);
        }

        vkDevice.updateDescriptorSets(writes, {});
    }

    vk::DescriptorSet GPUCullLODPipeline::allocateExternalDescriptorSet(vk::DescriptorPool externalPool,
                                                                         vk::Buffer externalCameraBuffer)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = externalPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        vk::DescriptorSet outSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

        ExternalDescriptorRef ref{};
        ref.descriptorSet = outSet;
        ref.cameraBuffer = externalCameraBuffer;
        writeBindingsToSet(ref);

        externalDescriptorSets.push_back(ref);
        return outSet;
    }

    vk::DescriptorSet GPUCullLODPipeline::allocateShadowDescriptorSet(
        vk::DescriptorPool externalPool, vk::Buffer cameraBuffer, vk::DeviceSize cameraOffset,
        vk::Buffer drawCommandBuffer, vk::Buffer perDrawDataBuffer, vk::Buffer drawCountBuffer)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = externalPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        vk::DescriptorSet outSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

        ExternalDescriptorRef ref{};
        ref.descriptorSet = outSet;
        ref.cameraBuffer = cameraBuffer;
        ref.cameraOffset = cameraOffset;
        ref.cameraRange = sizeof(GPUCameraData);
        ref.drawCommandOverride = drawCommandBuffer;
        ref.perDrawDataOverride = perDrawDataBuffer;
        ref.drawCountOverride = drawCountBuffer;
        writeBindingsToSet(ref);

        externalDescriptorSets.push_back(ref);
        return outSet;
    }

    void GPUCullLODPipeline::releaseExternalDescriptorSet(vk::DescriptorSet externalSet)
    {
        auto it = std::find_if(externalDescriptorSets.begin(), externalDescriptorSets.end(),
            [externalSet](const ExternalDescriptorRef& r) { return r.descriptorSet == externalSet; });
        if (it != externalDescriptorSets.end())
        {
            externalDescriptorSets.erase(it);
        }
    }

    void GPUCullLODPipeline::writeDescriptors()
    {
        if (!descriptorsNeedUpdate && !hiZDescriptorNeedsUpdate)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo objectInfo{};
        objectInfo.buffer = cachedObjectBuffer;
        objectInfo.offset = 0;
        objectInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = cachedCameraBuffer;
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(GPUCameraData);

        vk::DescriptorBufferInfo drawCmdInfo{};
        drawCmdInfo.buffer = cachedDrawCommandBuffer;
        drawCmdInfo.offset = 0;
        drawCmdInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo perDrawInfo{};
        perDrawInfo.buffer = cachedPerDrawDataBuffer;
        perDrawInfo.offset = 0;
        perDrawInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo drawCountInfo{};
        drawCountInfo.buffer = cachedDrawCountBuffer;
        drawCountInfo.offset = 0;
        drawCountInfo.range = VK_WHOLE_SIZE; // All batch stats

        vk::DescriptorBufferInfo activeIndexInfo{};
        activeIndexInfo.buffer = cachedActiveIndexBuffer;
        activeIndexInfo.offset = 0;
        activeIndexInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorImageInfo hiZInfo{};
        hiZInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        hiZInfo.imageView = cachedHiZView;
        hiZInfo.sampler = cachedHiZSampler;

        bool hasHiZ = cachedHiZView && cachedHiZSampler;

        std::vector<vk::WriteDescriptorSet> writes;
        writes.reserve(hasHiZ ? 7 : 6);

        vk::WriteDescriptorSet objectWrite{};
        objectWrite.dstSet = descriptorSet;
        objectWrite.dstBinding = 0;
        objectWrite.dstArrayElement = 0;
        objectWrite.descriptorCount = 1;
        objectWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        objectWrite.pBufferInfo = &objectInfo;
        writes.push_back(objectWrite);

        vk::WriteDescriptorSet cameraWrite{};
        cameraWrite.dstSet = descriptorSet;
        cameraWrite.dstBinding = 1;
        cameraWrite.dstArrayElement = 0;
        cameraWrite.descriptorCount = 1;
        cameraWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        cameraWrite.pBufferInfo = &cameraInfo;
        writes.push_back(cameraWrite);

        vk::WriteDescriptorSet drawCmdWrite{};
        drawCmdWrite.dstSet = descriptorSet;
        drawCmdWrite.dstBinding = 2;
        drawCmdWrite.dstArrayElement = 0;
        drawCmdWrite.descriptorCount = 1;
        drawCmdWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        drawCmdWrite.pBufferInfo = &drawCmdInfo;
        writes.push_back(drawCmdWrite);

        vk::WriteDescriptorSet perDrawWrite{};
        perDrawWrite.dstSet = descriptorSet;
        perDrawWrite.dstBinding = 3;
        perDrawWrite.dstArrayElement = 0;
        perDrawWrite.descriptorCount = 1;
        perDrawWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        perDrawWrite.pBufferInfo = &perDrawInfo;
        writes.push_back(perDrawWrite);

        vk::WriteDescriptorSet drawCountWrite{};
        drawCountWrite.dstSet = descriptorSet;
        drawCountWrite.dstBinding = 4;
        drawCountWrite.dstArrayElement = 0;
        drawCountWrite.descriptorCount = 1;
        drawCountWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        drawCountWrite.pBufferInfo = &drawCountInfo;
        writes.push_back(drawCountWrite);

        if (hasHiZ)
        {
            vk::WriteDescriptorSet hiZWrite{};
            hiZWrite.dstSet = descriptorSet;
            hiZWrite.dstBinding = 5;
            hiZWrite.dstArrayElement = 0;
            hiZWrite.descriptorCount = 1;
            hiZWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            hiZWrite.pImageInfo = &hiZInfo;
            writes.push_back(hiZWrite);
        }

        if (cachedActiveIndexBuffer)
        {
            vk::WriteDescriptorSet activeIndexWrite{};
            activeIndexWrite.dstSet = descriptorSet;
            activeIndexWrite.dstBinding = 6;
            activeIndexWrite.dstArrayElement = 0;
            activeIndexWrite.descriptorCount = 1;
            activeIndexWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
            activeIndexWrite.pBufferInfo = &activeIndexInfo;
            writes.push_back(activeIndexWrite);
        }

        vkDevice.updateDescriptorSets(writes, {});
        descriptorsNeedUpdate = false;
        hiZDescriptorNeedsUpdate = false;

        // Keep external (per-RTT) descriptor sets in sync with the cached buffers/hiZ. Each one
        // mirrors the main set except for binding 1 (camera buffer), which stays at its own
        // per-RTT buffer handle.
        for (const auto& ext : externalDescriptorSets)
        {
            writeBindingsToSet(ext);
        }
    }

    void GPUCullLODPipeline::dispatch(vk::CommandBuffer cmd, uint32_t objectCount)
    {
        if (!initialized || objectCount == 0)
        {
            return;
        }

        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, {});

        uint32_t groupCount = (objectCount + CULL_WORKGROUP_SIZE - 1) / CULL_WORKGROUP_SIZE;
        cmd.dispatch(groupCount, 1, 1);
    }

    void GPUCullLODPipeline::dispatchWithSet(vk::CommandBuffer cmd, uint32_t objectCount,
                                              vk::DescriptorSet externalSet)
    {
        if (!initialized || objectCount == 0 || !externalSet)
        {
            return;
        }

        // External set is kept in sync inside writeDescriptors(); call it to flush any pending
        // main-side updates (which also propagate to externals).
        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, externalSet, {});

        uint32_t groupCount = (objectCount + CULL_WORKGROUP_SIZE - 1) / CULL_WORKGROUP_SIZE;
        cmd.dispatch(groupCount, 1, 1);
    }
}
