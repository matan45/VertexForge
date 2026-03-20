#include "SVTFeedbackPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::svt
{
    SVTFeedbackPipeline::SVTFeedbackPipeline(core::Device& device)
        : device_(device)
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
        if (initialized_) return;
        config_ = config;
        totalPageTableEntries_ = computeTotalPageTableEntries(
            config_.virtualTextureSizeLog2, config_.tileSizeLog2);

        createDescriptorSetLayout();
        createPipeline();
        createFeedbackBuffers();
        createParamsBuffer();

        // Update descriptor sets with depth image
        updateDepthImage(depthImageView, depthSampler);

        initialized_ = true;
        vfLogInfo("SVT FeedbackPipeline initialized: {} page table entries", totalPageTableEntries_);
    }

    void SVTFeedbackPipeline::cleanup()
    {
        if (!initialized_) return;

        auto dev = device_.getLogicalDevice();
        dev.waitIdle();

        // Params buffer
        if (paramsMapped_)
        {
            dev.unmapMemory(paramsMemory_);
            paramsMapped_ = nullptr;
        }
        core::BufferUtilities::destroyBuffer(dev, paramsBuffer_, paramsMemory_);

        // Feedback buffers
        for (auto& frame : feedbackFrames_)
        {
            if (frame.feedbackMapped)
            {
                dev.unmapMemory(frame.feedbackMemory);
                frame.feedbackMapped = nullptr;
            }
            core::BufferUtilities::destroyBuffer(dev, frame.feedbackBuffer, frame.feedbackMemory);
        }

        if (descriptorPool_) dev.destroyDescriptorPool(descriptorPool_);
        if (pipeline_) dev.destroyPipeline(pipeline_);
        if (pipelineLayout_) dev.destroyPipelineLayout(pipelineLayout_);
        if (descriptorSetLayout_) dev.destroyDescriptorSetLayout(descriptorSetLayout_);

        if (shader_)
        {
            shader_->cleanUp();
            shader_.reset();
        }

        initialized_ = false;
    }

    void SVTFeedbackPipeline::updateDepthImage(vk::ImageView depthImageView,
                                                vk::Sampler depthSampler)
    {
        auto dev = device_.getLogicalDevice();

        for (uint32_t i = 0; i < FEEDBACK_BUFFER_COUNT; ++i)
        {
            auto& frame = feedbackFrames_[i];

            vk::DescriptorImageInfo depthInfo{};
            depthInfo.imageView = depthImageView;
            depthInfo.sampler = depthSampler;
            depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::DescriptorBufferInfo feedbackBufInfo{};
            feedbackBufInfo.buffer = frame.feedbackBuffer;
            feedbackBufInfo.offset = 0;
            feedbackBufInfo.range = totalPageTableEntries_ * sizeof(uint32_t);

            vk::DescriptorBufferInfo paramsBufInfo{};
            paramsBufInfo.buffer = paramsBuffer_;
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
        if (!initialized_) return;
        currentFrame_ = frameIndex;

        // Update params UBO
        SVTFeedbackParamsGPU params{};
        params.invViewProjection = invViewProjection;
        params.screenParams = screenParams;
        params.svtScaleOffset = glm::vec4(svtScale, svtOffset);
        params.svtInfo = glm::uvec4(
            config_.virtualTextureSizeLog2,
            config_.tileSizeLog2,
            0,
            computeMipLevelCount(config_.virtualTextureSizeLog2, config_.tileSizeLog2)
        );
        params.cameraPos = glm::vec4(cameraPos, 0.0f);
        std::memcpy(paramsMapped_, &params, sizeof(params));

        uint32_t wIdx = writeFrameIndex(frameIndex);
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline_);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout_,
                               0, feedbackFrames_[wIdx].descriptorSet, {});

        uint32_t groupsX = (static_cast<uint32_t>(screenParams.x) + 7) / 8;
        uint32_t groupsY = (static_cast<uint32_t>(screenParams.y) + 7) / 8;
        cmd.dispatch(groupsX, groupsY, 1);
    }

    const uint32_t* SVTFeedbackPipeline::readFeedback(uint64_t frameIndex) const
    {
        if (!initialized_) return nullptr;
        uint32_t rIdx = readFrameIndex(frameIndex);
        return static_cast<const uint32_t*>(feedbackFrames_[rIdx].feedbackMapped);
    }

    void SVTFeedbackPipeline::clearFeedbackBuffer(vk::CommandBuffer cmd, uint64_t frameIndex)
    {
        uint32_t wIdx = writeFrameIndex(frameIndex);
        cmd.fillBuffer(feedbackFrames_[wIdx].feedbackBuffer, 0,
                       totalPageTableEntries_ * sizeof(uint32_t), 0);
    }

    // ---- Private ----

    void SVTFeedbackPipeline::createDescriptorSetLayout()
    {
        auto dev = device_.getLogicalDevice();

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
        descriptorSetLayout_ = dev.createDescriptorSetLayout(layoutInfo);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
        pipelineLayout_ = dev.createPipelineLayout(pipelineLayoutInfo);
    }

    void SVTFeedbackPipeline::createPipeline()
    {
        shader_ = std::make_unique<core::Shader>(device_);
        shader_->readShader("../../resources/shaders/svt/svt_feedback.glsl");

        const auto& stages = shader_->getShaderStages();
        if (stages.empty())
        {
            vfLogError("Failed to load SVT feedback shader: {}", shader_->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout_;

        auto result = device_.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        pipeline_ = result.value;
    }

    void SVTFeedbackPipeline::createFeedbackBuffers()
    {
        auto dev = device_.getLogicalDevice();
        auto physDev = device_.getPhysicalDevice();

        vk::DeviceSize bufferSize = totalPageTableEntries_ * sizeof(uint32_t);

        // Create descriptor pool
        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, FEEDBACK_BUFFER_COUNT};
        poolSizes[1] = {vk::DescriptorType::eStorageBuffer, FEEDBACK_BUFFER_COUNT};
        poolSizes[2] = {vk::DescriptorType::eUniformBuffer, FEEDBACK_BUFFER_COUNT};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = FEEDBACK_BUFFER_COUNT;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool_ = dev.createDescriptorPool(poolInfo);

        for (uint32_t i = 0; i < FEEDBACK_BUFFER_COUNT; ++i)
        {
            auto& frame = feedbackFrames_[i];

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
            allocInfo.descriptorPool = descriptorPool_;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &descriptorSetLayout_;
            frame.descriptorSet = dev.allocateDescriptorSets(allocInfo)[0];
        }
    }

    void SVTFeedbackPipeline::createParamsBuffer()
    {
        auto dev = device_.getLogicalDevice();
        auto physDev = device_.getPhysicalDevice();

        core::BufferInfoRequest bufReq(dev, physDev,
            sizeof(SVTFeedbackParamsGPU),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(bufReq, paramsBuffer_, paramsMemory_);
        paramsMapped_ = dev.mapMemory(paramsMemory_, 0, sizeof(SVTFeedbackParamsGPU));
    }
}
