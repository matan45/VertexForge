#include "SVTFeedbackPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::svt
{
    SVTFeedbackPipeline::SVTFeedbackPipeline(core::Device& device)
        : device(device)
    {
    }

    SVTFeedbackPipeline::~SVTFeedbackPipeline()
    {
        cleanup();
    }

    void SVTFeedbackPipeline::init(const SVTConfig& config,
                                    vk::ImageView depthImageView,
                                    vk::Sampler depthSampler)
    {
        if (initialized) return;
        this->config = config;
        this->depthSampler = depthSampler;
        totalPageTableEntries = computeTotalPageTableEntries(
            config.virtualTextureSizeLog2, config.tileSizeLog2);

        createDescriptorSetLayout();
        createPipeline();
        createFeedbackBuffers();
        createParamsBuffer();

        // Update descriptor sets with depth image
        updateDepthImage(depthImageView, depthSampler);

        initialized = true;
        vfLogInfo("SVT FeedbackPipeline initialized: {} page table entries", totalPageTableEntries);
    }

    void SVTFeedbackPipeline::cleanup()
    {
        if (!initialized) return;

        auto dev = device.getLogicalDevice();
        dev.waitIdle();

        if (depthSampler) dev.destroySampler(depthSampler);

        // Params buffer
        if (paramsMapped)
        {
            dev.unmapMemory(paramsMemory);
            paramsMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsMemory);

        // Feedback buffers
        for (auto& frame : feedbackFrames)
        {
            if (frame.feedbackMapped)
            {
                dev.unmapMemory(frame.feedbackMemory);
                frame.feedbackMapped = nullptr;
            }
            core::BufferUtilities::destroyBuffer(dev, frame.feedbackBuffer, frame.feedbackMemory);
        }

        if (descriptorPool) dev.destroyDescriptorPool(descriptorPool);
        if (pipeline) dev.destroyPipeline(pipeline);
        if (pipelineLayout) dev.destroyPipelineLayout(pipelineLayout);
        if (descriptorSetLayout) dev.destroyDescriptorSetLayout(descriptorSetLayout);

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
    }

    void SVTFeedbackPipeline::updateDepthImage(vk::ImageView depthImageView,
                                                vk::Sampler depthSampler)
    {
        auto dev = device.getLogicalDevice();

        for (uint32_t i = 0; i < FEEDBACK_BUFFER_COUNT; ++i)
        {
            auto& frame = feedbackFrames[i];

            vk::DescriptorImageInfo depthInfo{};
            depthInfo.imageView = depthImageView;
            depthInfo.sampler = depthSampler;
            depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::DescriptorBufferInfo feedbackBufInfo{};
            feedbackBufInfo.buffer = frame.feedbackBuffer;
            feedbackBufInfo.offset = 0;
            feedbackBufInfo.range = totalPageTableEntries * sizeof(uint32_t);

            vk::DescriptorBufferInfo paramsBufInfo{};
            paramsBufInfo.buffer = paramsBuffer;
            paramsBufInfo.offset = 0;
            paramsBufInfo.range = sizeof(SVTFeedbackParamsGPU);

            std::array<vk::WriteDescriptorSet, 3> writes{};

            writes[0].dstSet = frame.descriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].pImageInfo = &depthInfo;

            writes[1].dstSet = frame.descriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[1].pBufferInfo = &feedbackBufInfo;

            writes[2].dstSet = frame.descriptorSet;
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[2].pBufferInfo = &paramsBufInfo;

            dev.updateDescriptorSets(writes, {});
        }
    }

    void SVTFeedbackPipeline::dispatch(vk::CommandBuffer cmd, uint64_t frameIndex,
                                        const glm::mat4& invViewProjection,
                                        const glm::vec4& screenParams,
                                        const glm::vec3& cameraPos,
                                        const glm::vec2& svtScale,
                                        const glm::vec2& svtOffset)
    {
        if (!initialized) return;
        currentFrame = frameIndex;

        // Update params UBO
        SVTFeedbackParamsGPU params{};
        params.invViewProjection = invViewProjection;
        params.screenParams = screenParams;
        params.svtScaleOffset = glm::vec4(svtScale, svtOffset);
        params.svtInfo = glm::uvec4(
            config.virtualTextureSizeLog2,
            config.tileSizeLog2,
            0,
            computeMipLevelCount(config.virtualTextureSizeLog2, config.tileSizeLog2)
        );
        params.cameraPos = glm::vec4(cameraPos, 0.0f);
        std::memcpy(paramsMapped, &params, sizeof(params));

        uint32_t wIdx = writeFrameIndex(frameIndex);
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, feedbackFrames[wIdx].descriptorSet, {});

        uint32_t groupsX = (static_cast<uint32_t>(screenParams.x) + 7) / 8;
        uint32_t groupsY = (static_cast<uint32_t>(screenParams.y) + 7) / 8;
        cmd.dispatch(groupsX, groupsY, 1);
    }

    const uint32_t* SVTFeedbackPipeline::readFeedback(uint64_t frameIndex) const
    {
        if (!initialized) return nullptr;
        uint32_t rIdx = readFrameIndex(frameIndex);
        return static_cast<const uint32_t*>(feedbackFrames[rIdx].feedbackMapped);
    }

    void SVTFeedbackPipeline::clearFeedbackBuffer(vk::CommandBuffer cmd, uint64_t frameIndex)
    {
        uint32_t wIdx = writeFrameIndex(frameIndex);
        cmd.fillBuffer(feedbackFrames[wIdx].feedbackBuffer, 0,
                       totalPageTableEntries * sizeof(uint32_t), 0);
    }

    // ---- Private ----

    void SVTFeedbackPipeline::createDescriptorSetLayout()
    {
        auto dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

        // Binding 0: depth buffer sampler
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 1: feedback SSBO
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 2: params UBO
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayout = dev.createPipelineLayout(pipelineLayoutInfo);
    }

    void SVTFeedbackPipeline::createPipeline()
    {
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/svt/svt_feedback.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("Failed to load SVT feedback shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = device.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        pipeline = result.value;
    }

    void SVTFeedbackPipeline::createFeedbackBuffers()
    {
        auto dev = device.getLogicalDevice();
        auto physDev = device.getPhysicalDevice();

        vk::DeviceSize bufferSize = totalPageTableEntries * sizeof(uint32_t);

        // Create descriptor pool
        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, FEEDBACK_BUFFER_COUNT};
        poolSizes[1] = {vk::DescriptorType::eStorageBuffer, FEEDBACK_BUFFER_COUNT};
        poolSizes[2] = {vk::DescriptorType::eUniformBuffer, FEEDBACK_BUFFER_COUNT};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = FEEDBACK_BUFFER_COUNT;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = dev.createDescriptorPool(poolInfo);

        for (uint32_t i = 0; i < FEEDBACK_BUFFER_COUNT; ++i)
        {
            auto& frame = feedbackFrames[i];

            // Host-visible feedback buffer for CPU readback
            core::BufferInfoRequest bufReq(dev, physDev,
                bufferSize,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            );
            core::BufferUtilities::createBuffer(bufReq, frame.feedbackBuffer, frame.feedbackMemory);
            frame.feedbackMapped = dev.mapMemory(frame.feedbackMemory, 0, bufferSize);
            std::memset(frame.feedbackMapped, 0, bufferSize);

            // Allocate descriptor set
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &descriptorSetLayout;
            frame.descriptorSet = dev.allocateDescriptorSets(allocInfo)[0];
        }
    }

    void SVTFeedbackPipeline::createParamsBuffer()
    {
        auto dev = device.getLogicalDevice();
        auto physDev = device.getPhysicalDevice();

        core::BufferInfoRequest bufReq(dev, physDev,
            sizeof(SVTFeedbackParamsGPU),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsMemory);
        paramsMapped = dev.mapMemory(paramsMemory, 0, sizeof(SVTFeedbackParamsGPU));
    }
}
