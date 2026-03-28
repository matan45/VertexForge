#include "VSMFeedbackPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

namespace render::shadow
{

    VSMFeedbackPipeline::VSMFeedbackPipeline(core::Device& device)
        : device(device)
    {
    }

    VSMFeedbackPipeline::~VSMFeedbackPipeline()
    {
        cleanup();
    }

    void VSMFeedbackPipeline::init(uint32_t maxFeedbackEntries)
    {
        if (initialized)
            return;

        totalFeedbackEntries = maxFeedbackEntries;

        createBuffers();
        createDepthSampler();
        createComputePipeline();
        createDescriptorSets();

        initialized = true;
        vfLogInfo("VSM feedback pipeline initialized with {} max entries", totalFeedbackEntries);
    }

    void VSMFeedbackPipeline::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        if (computePipeline)
        {
            logicalDevice.destroyPipeline(computePipeline);
            computePipeline = nullptr;
        }
        if (pipelineLayout)
        {
            logicalDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (descriptorPool)
        {
            logicalDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        if (depthSampler)
        {
            logicalDevice.destroySampler(depthSampler);
            depthSampler = nullptr;
        }

        {
            auto& memManager = device.getMemoryManager();
            core::BufferUtilities::destroyBuffer(logicalDevice, paramsBuffer, paramsAllocation, memManager);
            core::BufferUtilities::destroyBuffer(logicalDevice, stagingBuffer, stagingAllocation, memManager);
            core::BufferUtilities::destroyBuffer(logicalDevice, feedbackBuffer, feedbackAllocation, memManager);
        }

        initialized = false;
    }

    void VSMFeedbackPipeline::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();
        auto& memManager = device.getMemoryManager();
        vk::DeviceSize feedbackSize = sizeof(uint32_t) * totalFeedbackEntries;

        // Device-local feedback buffer
        {
            core::BufferInfoRequest request(
                logicalDevice, physicalDevice, feedbackSize,
                vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eTransferSrc |
                vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            );
            core::BufferUtilities::createBuffer(request, feedbackBuffer, feedbackAllocation, memManager);
        }

        // Host-visible staging buffer for readback
        {
            core::BufferInfoRequest request(
                logicalDevice, physicalDevice, feedbackSize,
                vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            );
            core::BufferUtilities::createBuffer(request, stagingBuffer, stagingAllocation, memManager);
        }

        // UBO for params
        {
            core::BufferInfoRequest request(
                logicalDevice, physicalDevice, sizeof(FeedbackParams),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            );
            core::BufferUtilities::createBuffer(request, paramsBuffer, paramsAllocation, memManager);
        }
    }

    void VSMFeedbackPipeline::createDepthSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        depthSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void VSMFeedbackPipeline::createComputePipeline()
    {
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/shadow/vsm_feedback.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("Failed to load VSM feedback shader: {}", shader->getLastCompilationError());
            return;
        }

        // Binding 0: depth sampler
        // Binding 1: VSM light SSBO
        // Binding 2: feedback SSBO
        // Binding 3: params UBO
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
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
        bindings[3].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        computePipeline = core::PipelineUtilities::createComputePipeline(device.getLogicalDevice(), pipelineInfo);
        if (!computePipeline)
        {
            vfLogError("VSMFeedbackPipeline: Failed to create compute pipeline");
            return;
        }
    }

    void VSMFeedbackPipeline::createDescriptorSets()
    {
        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[1].descriptorCount = 2;
        poolSizes[2].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[2].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

    }

    void VSMFeedbackPipeline::clearFeedbackBuffer(vk::CommandBuffer cmd)
    {
        if (!initialized)
            return;

        cmd.fillBuffer(feedbackBuffer, 0, sizeof(uint32_t) * totalFeedbackEntries, 0);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = feedbackBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(uint32_t) * totalFeedbackEntries;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, barrier, {}
        );
    }

    void VSMFeedbackPipeline::dispatch(vk::CommandBuffer cmd,
                                        vk::ImageView depthView,
                                        vk::Buffer shadowDataBuffer, vk::DeviceSize shadowDataSize,
                                        uint32_t lightCount,
                                        const glm::mat4& invViewProjection,
                                        uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !computePipeline || lightCount == 0)
            return;

        // Update params UBO
        FeedbackParams params{};
        params.invViewProjection = invViewProjection;
        params.screenParams = glm::vec4(
            static_cast<float>(screenWidth),
            static_cast<float>(screenHeight),
            1.0f / static_cast<float>(screenWidth),
            1.0f / static_cast<float>(screenHeight)
        );
        params.lightCount = lightCount;

        std::memcpy(paramsAllocation.mappedPtr, &params, sizeof(FeedbackParams));

        // Update descriptor set — depth view can change per frame (resize, etc.)
        // so we always update. The needsDescriptorUpdate flag is not needed here.
        {
            vk::DescriptorImageInfo depthInfo{};
            depthInfo.sampler = depthSampler;
            depthInfo.imageView = depthView;
            depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::DescriptorBufferInfo lightInfo{};
            lightInfo.buffer = shadowDataBuffer;
            lightInfo.offset = 0;
            lightInfo.range = shadowDataSize;

            vk::DescriptorBufferInfo feedbackInfo{};
            feedbackInfo.buffer = feedbackBuffer;
            feedbackInfo.offset = 0;
            feedbackInfo.range = sizeof(uint32_t) * totalFeedbackEntries;

            vk::DescriptorBufferInfo paramsInfo{};
            paramsInfo.buffer = paramsBuffer;
            paramsInfo.offset = 0;
            paramsInfo.range = sizeof(FeedbackParams);

            std::array<vk::WriteDescriptorSet, 4> writes{};

            writes[0].dstSet = descriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &depthInfo;

            writes[1].dstSet = descriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &lightInfo;

            writes[2].dstSet = descriptorSet;
            writes[2].dstBinding = 2;
            writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo = &feedbackInfo;

            writes[3].dstSet = descriptorSet;
            writes[3].dstBinding = 3;
            writes[3].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[3].descriptorCount = 1;
            writes[3].pBufferInfo = &paramsInfo;

            device.getLogicalDevice().updateDescriptorSets(writes, {});
        }

        // Dispatch compute shader
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, {});

        uint32_t groupsX = (screenWidth + 7) / 8;
        uint32_t groupsY = (screenHeight + 7) / 8;
        cmd.dispatch(groupsX, groupsY, 1);

        // Barrier: compute write -> transfer read
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = feedbackBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(uint32_t) * totalFeedbackEntries;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer,
            {}, {}, barrier, {}
        );
    }

    void VSMFeedbackPipeline::copyResultsToStaging(vk::CommandBuffer cmd)
    {
        if (!initialized)
            return;

        vk::BufferCopy copyRegion{};
        copyRegion.size = sizeof(uint32_t) * totalFeedbackEntries;
        cmd.copyBuffer(feedbackBuffer, stagingBuffer, copyRegion);

        readbackState = FeedbackReadbackState::Pending;
    }

    void VSMFeedbackPipeline::markResultsReady()
    {
        if (readbackState == FeedbackReadbackState::Pending)
            readbackState = FeedbackReadbackState::Ready;
    }

    std::vector<uint32_t> VSMFeedbackPipeline::readbackResults(uint32_t count)
    {
        std::vector<uint32_t> results;

        if (readbackState != FeedbackReadbackState::Ready || !initialized)
            return results;

        count = std::min(count, totalFeedbackEntries);
        results.resize(count);

        std::memcpy(results.data(), stagingAllocation.mappedPtr, sizeof(uint32_t) * count);

        readbackState = FeedbackReadbackState::Idle;
        return results;
    }
}
