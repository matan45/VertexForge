#include "AutoExposureEffect.hpp"
#include "../PostProcessPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace render::postprocess
{
    struct HistogramPushConstants
    {
        float minLogLuminance;
        float logLuminanceRange;
        uint32_t width;
        uint32_t height;
    };

    struct ReducePushConstants
    {
        float minLogLuminance;
        float logLuminanceRange;
        float lowPercentile;
        float highPercentile;
        float adaptSpeedUp;
        float adaptSpeedDown;
        float deltaTime;
        float exposureCompensation;
        float minExposure;
        float maxExposure;
        uint32_t pixelCount;
    };

    struct ExposureData
    {
        float currentExposure;
        float targetExposure;
        float avgLuminance;
        float padding;
    };

    AutoExposureEffect::AutoExposureEffect(core::Device& device, core::SwapChain& swapChain,
                                           core::OffscreenResources& offscreenResources,
                                           PostProcessPipeline& pipeline)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}, pipeline{pipeline}
    {
        enabled = true;
    }

    void AutoExposureEffect::init(vk::Format colorFormat, vk::Extent2D extent)
    {
        currentExtent = extent;
        createSampler();
        createBuffers();
        loadShaders();
        createDescriptorSetLayouts();
        createDescriptorPool();
        createDescriptorSets();
        createHistogramPipeline();
        createReducePipeline();
        createPassthroughPipeline(colorFormat, extent);

        ExposureData initialData{1.0f, 1.0f, 0.18f, 0.0f};
        for (auto& ef : exposureFrames)
        {
            std::memcpy(ef.mapped, &initialData, sizeof(ExposureData));
        }

        initialized = true;
    }

    void AutoExposureEffect::cleanup()
    {
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupBuffers();

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (histogramDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(histogramDescriptorSetLayout);
            histogramDescriptorSetLayout = nullptr;
        }

        if (reduceDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(reduceDescriptorSetLayout);
            reduceDescriptorSetLayout = nullptr;
        }

        if (passthroughDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(passthroughDescriptorSetLayout);
            passthroughDescriptorSetLayout = nullptr;
        }

        if (sceneSampler)
        {
            dev.destroySampler(sceneSampler);
            sceneSampler = nullptr;
        }

        if (histogramShader)
        {
            histogramShader->cleanUp();
            histogramShader.reset();
        }

        if (reduceShader)
        {
            reduceShader->cleanUp();
            reduceShader.reset();
        }

        if (passthroughShader)
        {
            passthroughShader->cleanUp();
            passthroughShader.reset();
        }

        initialized = false;
    }

    void AutoExposureEffect::recreate(vk::Format colorFormat, vk::Extent2D extent)
    {
        currentExtent = extent;
        lastSceneImageView = nullptr;
        cleanupPipelines();

        auto& dev = device.getLogicalDevice();
        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        createDescriptorPool();
        createDescriptorSets();
        createHistogramPipeline();
        createReducePipeline();
        createPassthroughPipeline(colorFormat, extent);
    }

    void AutoExposureEffect::preRecord(const vk::CommandBuffer& commandBuffer,
                                        vk::DescriptorSet inputDescriptorSet)
    {
        // Read exposure from the previous frame's buffer
        uint32_t readFrame = (currentExposureFrame + core::MAX_FRAMES_IN_FLIGHT - 1) % core::MAX_FRAMES_IN_FLIGHT;
        auto* readMapped = static_cast<ExposureData*>(exposureFrames[readFrame].mapped);
        computedExposure = readMapped->currentExposure;
        if (computedExposure <= 0.0f || std::isnan(computedExposure))
            computedExposure = 1.0f;

        float currentTime = pipeline.getCameraData().time;
        float deltaTime = firstFrame ? 0.016f : (currentTime - previousTime);
        deltaTime = std::clamp(deltaTime, 0.001f, 0.5f);
        previousTime = currentTime;
        firstFrame = false;

        // Update histogram descriptor with the current scene image view
        updateHistogramDescriptorSet(offscreenResources.colorImages[0].colorImageView);

        // Clear histogram buffer
        commandBuffer.fillBuffer(histogramBuffer, 0, 256 * sizeof(uint32_t), 0);

        // Barrier: transfer → compute
        vk::BufferMemoryBarrier clearBarrier{};
        clearBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        clearBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        clearBarrier.buffer = histogramBuffer;
        clearBarrier.size = VK_WHOLE_SIZE;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, clearBarrier, {});

        // Dispatch histogram build
        constexpr float MIN_LOG_LUMINANCE = -10.0f;
        constexpr float MAX_LOG_LUMINANCE = 12.0f;
        constexpr float LOG_LUMINANCE_RANGE = MAX_LOG_LUMINANCE - MIN_LOG_LUMINANCE;

        HistogramPushConstants histPC{};
        histPC.minLogLuminance = MIN_LOG_LUMINANCE;
        histPC.logLuminanceRange = LOG_LUMINANCE_RANGE;
        histPC.width = currentExtent.width;
        histPC.height = currentExtent.height;

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, histogramPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, histogramPipelineLayout,
                                          0, histogramDescriptorSet, nullptr);
        commandBuffer.pushConstants(histogramPipelineLayout, vk::ShaderStageFlagBits::eCompute,
                                     0, sizeof(HistogramPushConstants), &histPC);

        uint32_t groupsX = (currentExtent.width + 15) / 16;
        uint32_t groupsY = (currentExtent.height + 15) / 16;
        commandBuffer.dispatch(groupsX, groupsY, 1);

        // Barrier: compute write → compute read
        vk::BufferMemoryBarrier histBarrier{};
        histBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        histBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        histBarrier.buffer = histogramBuffer;
        histBarrier.size = VK_WHOLE_SIZE;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, histBarrier, {});

        // Update reduce descriptor set with current frame's exposure buffer
        updateReduceDescriptorSet();

        // Dispatch histogram reduce
        ReducePushConstants reducePC{};
        reducePC.minLogLuminance = MIN_LOG_LUMINANCE;
        reducePC.logLuminanceRange = LOG_LUMINANCE_RANGE;
        reducePC.lowPercentile = lowPercentile;
        reducePC.highPercentile = highPercentile;
        reducePC.adaptSpeedUp = adaptSpeedUp;
        reducePC.adaptSpeedDown = adaptSpeedDown;
        reducePC.deltaTime = deltaTime;
        reducePC.exposureCompensation = exposureCompensation;
        reducePC.minExposure = minExposure;
        reducePC.maxExposure = maxExposure;
        reducePC.pixelCount = currentExtent.width * currentExtent.height;

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, reducePipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, reducePipelineLayout,
                                          0, reduceDescriptorSet, nullptr);
        commandBuffer.pushConstants(reducePipelineLayout, vk::ShaderStageFlagBits::eCompute,
                                     0, sizeof(ReducePushConstants), &reducePC);

        commandBuffer.dispatch(1, 1, 1);

        // Barrier: compute write → host read (for next frame's readback)
        vk::BufferMemoryBarrier expBarrier{};
        expBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        expBarrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        expBarrier.buffer = exposureFrames[currentExposureFrame].buffer;
        expBarrier.size = VK_WHOLE_SIZE;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eHost,
            {}, {}, expBarrier, {});

        // Advance to next frame
        currentExposureFrame = (currentExposureFrame + 1) % core::MAX_FRAMES_IN_FLIGHT;
    }

    void AutoExposureEffect::record(const vk::CommandBuffer& commandBuffer,
                                     vk::DescriptorSet inputDescriptorSet)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, passthroughPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, passthroughPipelineLayout,
                                          0, inputDescriptorSet, nullptr);
        commandBuffer.draw(3, 1, 0, 0);
        render::FrameDrawStats::count(render::DrawCategory::PostProcess);
    }

    void AutoExposureEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& ae = settings.autoExposure;
        enabled = ae.enabled;
        minExposure = ae.minExposure;
        maxExposure = ae.maxExposure;
        adaptSpeedUp = ae.adaptSpeedUp;
        adaptSpeedDown = ae.adaptSpeedDown;
        exposureCompensation = ae.exposureCompensation;
        lowPercentile = ae.lowPercentile;
        highPercentile = ae.highPercentile;
    }

    void AutoExposureEffect::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        sceneSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void AutoExposureEffect::createBuffers()
    {
        auto& dev = device.getLogicalDevice();

        // Histogram buffer - device local storage buffer
        core::BufferInfoRequest histReq(dev, device.getPhysicalDevice());
        histReq.size = 256 * sizeof(uint32_t);
        histReq.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        histReq.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::BufferUtilities::createBuffer(histReq, histogramBuffer, histogramBufferAllocation, device.getMemoryManager());

        // Exposure buffers - host visible for CPU readback (per-frame)
        for (auto& ef : exposureFrames)
        {
            core::BufferInfoRequest expReq(dev, device.getPhysicalDevice());
            expReq.size = sizeof(ExposureData);
            expReq.usage = vk::BufferUsageFlagBits::eStorageBuffer;
            expReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

            core::BufferUtilities::createBuffer(expReq, ef.buffer, ef.allocation, device.getMemoryManager());
            ef.mapped = ef.allocation.mappedPtr;
        }
    }

    void AutoExposureEffect::createDescriptorSetLayouts()
    {
        auto& dev = device.getLogicalDevice();

        // Histogram layout: sampler2D (binding 0) + SSBO histogram (binding 1)
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

            histogramDescriptorSetLayout = core::PipelineUtilities::createUpdateAfterBindLayout(dev, bindings.data(), static_cast<uint32_t>(bindings.size()));
        }

        // Reduce layout: SSBO histogram (binding 0) + SSBO exposure (binding 1)
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

            reduceDescriptorSetLayout = core::PipelineUtilities::createUpdateAfterBindLayout(dev, bindings.data(), static_cast<uint32_t>(bindings.size()));
        }

        // Passthrough layout: sampler2D (binding 0) - for the graphics passthrough
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            passthroughDescriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
        }
    }

    void AutoExposureEffect::createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[1].descriptorCount = 4;

        descriptorPool = core::PipelineUtilities::createUpdateAfterBindPool(device.getLogicalDevice(), 2, poolSizes.data(), static_cast<uint32_t>(poolSizes.size()), vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    }

    void AutoExposureEffect::createDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();

        // Allocate histogram descriptor set
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &histogramDescriptorSetLayout;

            histogramDescriptorSet = dev.allocateDescriptorSets(allocInfo)[0];
        }

        // Allocate reduce descriptor set
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &reduceDescriptorSetLayout;

            reduceDescriptorSet = dev.allocateDescriptorSets(allocInfo)[0];
        }

        // Update reduce descriptor set with histogram buffer (exposure buffer updated per-frame in preRecord)
        updateReduceDescriptorSet();
    }

    void AutoExposureEffect::updateHistogramDescriptorSet(vk::ImageView sceneImageView)
    {
        if (sceneImageView == lastSceneImageView)
            return;
        lastSceneImageView = sceneImageView;

        auto& dev = device.getLogicalDevice();

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = sceneImageView;
        imageInfo.sampler = sceneSampler;

        vk::DescriptorBufferInfo histBufInfo{};
        histBufInfo.buffer = histogramBuffer;
        histBufInfo.offset = 0;
        histBufInfo.range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = histogramDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &imageInfo;

        writes[1].dstSet = histogramDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &histBufInfo;

        dev.updateDescriptorSets(writes, nullptr);
    }

    void AutoExposureEffect::updateReduceDescriptorSet()
    {
        auto& dev = device.getLogicalDevice();

        vk::DescriptorBufferInfo histBufInfo{};
        histBufInfo.buffer = histogramBuffer;
        histBufInfo.offset = 0;
        histBufInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo expBufInfo{};
        expBufInfo.buffer = exposureFrames[currentExposureFrame].buffer;
        expBufInfo.offset = 0;
        expBufInfo.range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 2> reduceWrites{};
        reduceWrites[0].dstSet = reduceDescriptorSet;
        reduceWrites[0].dstBinding = 0;
        reduceWrites[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        reduceWrites[0].descriptorCount = 1;
        reduceWrites[0].pBufferInfo = &histBufInfo;

        reduceWrites[1].dstSet = reduceDescriptorSet;
        reduceWrites[1].dstBinding = 1;
        reduceWrites[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        reduceWrites[1].descriptorCount = 1;
        reduceWrites[1].pBufferInfo = &expBufInfo;

        dev.updateDescriptorSets(reduceWrites, nullptr);
    }

    void AutoExposureEffect::loadShaders()
    {
        histogramShader = std::make_shared<core::Shader>(device);
        histogramShader->readShader("../../resources/shaders/postprocess/auto_exposure_histogram.glsl");

        reduceShader = std::make_shared<core::Shader>(device);
        reduceShader->readShader("../../resources/shaders/postprocess/auto_exposure_reduce.glsl");

        passthroughShader = std::make_shared<core::Shader>(device);
        passthroughShader->readShader("../../resources/shaders/postprocess/auto_exposure_passthrough.glsl");
    }

    void AutoExposureEffect::createHistogramPipeline()
    {
        auto& dev = device.getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(HistogramPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &histogramDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        histogramPipelineLayout = dev.createPipelineLayout(layoutInfo);

        auto& stages = histogramShader->getShaderStages();
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = histogramPipelineLayout;

        histogramPipeline = core::PipelineUtilities::createComputePipeline(dev, pipelineInfo);
    }

    void AutoExposureEffect::createReducePipeline()
    {
        auto& dev = device.getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(ReducePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &reduceDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        reducePipelineLayout = dev.createPipelineLayout(layoutInfo);

        auto& stages = reduceShader->getShaderStages();
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = reducePipelineLayout;

        reducePipeline = core::PipelineUtilities::createComputePipeline(dev, pipelineInfo);
    }

    void AutoExposureEffect::createPassthroughPipeline(vk::Format colorFormat, vk::Extent2D extent)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.colorAttachmentFormats = {colorFormat};
        config.extent = extent;
        config.shaderStages = passthroughShader->getShaderStages();
        config.descriptorSetLayouts = {passthroughDescriptorSetLayout};
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendEnable = false;
        config.cullMode = vk::CullModeFlagBits::eNone;

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        passthroughPipeline = result.pipeline;
        passthroughPipelineLayout = result.pipelineLayout;
    }

    void AutoExposureEffect::cleanupBuffers()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& ef : exposureFrames)
        {
            ef.mapped = nullptr;
            core::BufferUtilities::destroyBuffer(dev, ef.buffer, ef.allocation, device.getMemoryManager());
        }

        core::BufferUtilities::destroyBuffer(dev, histogramBuffer, histogramBufferAllocation, device.getMemoryManager());
    }

    void AutoExposureEffect::cleanupPipelines()
    {
        auto& dev = device.getLogicalDevice();

        if (histogramPipeline)
        {
            dev.destroyPipeline(histogramPipeline);
            histogramPipeline = nullptr;
        }
        if (histogramPipelineLayout)
        {
            dev.destroyPipelineLayout(histogramPipelineLayout);
            histogramPipelineLayout = nullptr;
        }

        if (reducePipeline)
        {
            dev.destroyPipeline(reducePipeline);
            reducePipeline = nullptr;
        }
        if (reducePipelineLayout)
        {
            dev.destroyPipelineLayout(reducePipelineLayout);
            reducePipelineLayout = nullptr;
        }

        if (passthroughPipeline)
        {
            dev.destroyPipeline(passthroughPipeline);
            passthroughPipeline = nullptr;
        }
        if (passthroughPipelineLayout)
        {
            dev.destroyPipelineLayout(passthroughPipelineLayout);
            passthroughPipelineLayout = nullptr;
        }
    }

}
