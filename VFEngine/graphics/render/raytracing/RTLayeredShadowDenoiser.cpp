#include "RTLayeredShadowDenoiser.hpp"
#include "RTShadowDenoiser.hpp" // ShadowDenoiserUBO + ShadowSpatialPushConstants (shared layouts)
#include <algorithm>
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::raytracing
{
    RTLayeredShadowDenoiser::RTLayeredShadowDenoiser(core::Device& device)
        : device(device)
    {
    }

    RTLayeredShadowDenoiser::~RTLayeredShadowDenoiser()
    {
        cleanup();
    }

    void RTLayeredShadowDenoiser::init(uint32_t width, uint32_t height)
    {
        if (initialized) return;

        maskWidth = width;
        maskHeight = height;

        createImages(width, height);
        createSamplers();
        createParamsBuffer();
        createDescriptorLayouts();
        createDescriptorPools();
        allocateDescriptorSets();
        createTemporalPipeline();
        createSpatialPipeline();
        createDenoisedMaskSamplerDescriptor();

        initialized = true;
        historyImagesInGeneral = false;
        spatialImagesReady = false;
        for (uint32_t s = 0; s < MAX_SLICES; ++s) sliceHistoryValid[s] = false;
        vfLogInfo("RTLayeredShadowDenoiser: Initialized {}x{} x{} slices", width, height, MAX_SLICES);
    }

    void RTLayeredShadowDenoiser::cleanup()
    {
        if (!initialized) return;
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        vkDevice.destroyPipeline(temporalPipeline);
        vkDevice.destroyPipelineLayout(temporalPipelineLayout);
        vkDevice.destroyPipeline(spatialPipeline);
        vkDevice.destroyPipelineLayout(spatialPipelineLayout);

        vkDevice.destroyDescriptorPool(temporalDSPool);
        vkDevice.destroyDescriptorPool(spatialDSPool);
        vkDevice.destroyDescriptorPool(denoisedMaskSamplerPool);
        vkDevice.destroyDescriptorSetLayout(temporalDSLayout);
        vkDevice.destroyDescriptorSetLayout(spatialDSLayout);
        vkDevice.destroyDescriptorSetLayout(denoisedMaskSamplerLayout);

        vkDevice.destroySampler(nearestSampler);
        vkDevice.destroySampler(denoisedMaskSampler);

        paramsBuffer.destroy(vkDevice, device.getMemoryManager());

        destroyImages();

        temporalShader.reset();
        spatialShader.reset();
        initialized = false;
    }

    void RTLayeredShadowDenoiser::resize(uint32_t width, uint32_t height)
    {
        if (!initialized) return;
        if (width == maskWidth && height == maskHeight) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        destroyImages();
        createImages(width, height);
        createDenoisedMaskSamplerDescriptor();

        historyImagesInGeneral = false;
        spatialImagesReady = false;
        for (uint32_t s = 0; s < MAX_SLICES; ++s) sliceHistoryValid[s] = false;
        vfLogInfo("RTLayeredShadowDenoiser: Resized to {}x{}", width, height);
    }

    vk::ImageView RTLayeredShadowDenoiser::makeLayerView(vk::Image image, vk::Format format, uint32_t layer)
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = layer;
        viewInfo.subresourceRange.layerCount = 1;
        return device.getLogicalDevice().createImageView(viewInfo);
    }

    void RTLayeredShadowDenoiser::createImages(uint32_t w, uint32_t h)
    {
        maskWidth = w;
        maskHeight = h;

        // History ping-pong (R16Sfloat array, one layer per slice)
        for (int i = 0; i < 2; ++i)
        {
            core::ImageInfoRequest request(
                device.getLogicalDevice(), device.getPhysicalDevice(),
                w, h, MAX_SLICES, 1,
                vk::Format::eR16Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            );
            core::ImageUtilities::createImage(request, history[i].image, history[i].allocation, device.getMemoryManager());
            for (uint32_t k = 0; k < MAX_SLICES; ++k)
                history[i].layerView[k] = makeLayerView(history[i].image, vk::Format::eR16Sfloat, k);
        }

        // Spatial ping-pong (R16Sfloat, plain 2D scratch shared across slices)
        for (int i = 0; i < 2; ++i)
        {
            core::ImageInfoRequest request(
                device.getLogicalDevice(), device.getPhysicalDevice(),
                w, h, 1, 1,
                vk::Format::eR16Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            );
            core::ImageUtilities::createImage(request, spatialBuf[i].image, spatialBuf[i].allocation, device.getMemoryManager());
            core::ImageViewInfoRequest viewReq(
                device.getLogicalDevice(), spatialBuf[i].image,
                vk::Format::eR16Sfloat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2D, 1, 1
            );
            core::ImageUtilities::createImageView(viewReq, spatialBuf[i].storageView);
        }

        // Denoised output (R16Sfloat array, one layer per slice)
        {
            core::ImageInfoRequest request(
                device.getLogicalDevice(), device.getPhysicalDevice(),
                w, h, MAX_SLICES, 1,
                vk::Format::eR16Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            );
            core::ImageUtilities::createImage(request, denoisedOutputImage, denoisedOutputAllocation, device.getMemoryManager());
            for (uint32_t k = 0; k < MAX_SLICES; ++k)
                denoisedOutputLayerView[k] = makeLayerView(denoisedOutputImage, vk::Format::eR16Sfloat, k);

            core::ImageViewInfoRequest arrayViewReq(
                device.getLogicalDevice(), denoisedOutputImage,
                vk::Format::eR16Sfloat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2DArray, MAX_SLICES, 1
            );
            core::ImageUtilities::createImageView(arrayViewReq, denoisedOutputArraySampledView);
        }
    }

    void RTLayeredShadowDenoiser::destroyImages()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        for (int i = 0; i < 2; ++i)
        {
            for (uint32_t k = 0; k < MAX_SLICES; ++k)
                vkDevice.destroyImageView(history[i].layerView[k]);
            vkDevice.destroyImage(history[i].image);
            device.getMemoryManager().free(history[i].allocation);
            history[i] = {};
        }

        for (int i = 0; i < 2; ++i)
        {
            vkDevice.destroyImageView(spatialBuf[i].storageView);
            vkDevice.destroyImage(spatialBuf[i].image);
            device.getMemoryManager().free(spatialBuf[i].allocation);
            spatialBuf[i] = {};
        }

        for (uint32_t k = 0; k < MAX_SLICES; ++k)
            vkDevice.destroyImageView(denoisedOutputLayerView[k]);
        vkDevice.destroyImageView(denoisedOutputArraySampledView);
        vkDevice.destroyImage(denoisedOutputImage);
        device.getMemoryManager().free(denoisedOutputAllocation);
        denoisedOutputAllocation = {};
    }

    void RTLayeredShadowDenoiser::createSamplers()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        nearestSampler = vkDevice.createSampler(samplerInfo);

        vk::SamplerCreateInfo linearInfo = samplerInfo;
        linearInfo.magFilter = vk::Filter::eLinear;
        linearInfo.minFilter = vk::Filter::eLinear;
        denoisedMaskSampler = vkDevice.createSampler(linearInfo);
    }

    void RTLayeredShadowDenoiser::createParamsBuffer()
    {
        paramsBuffer.create(device.getLogicalDevice(), device.getPhysicalDevice(),
                            sizeof(ShadowDenoiserUBO), device.getMemoryManager());
    }

    void RTLayeredShadowDenoiser::createDescriptorLayouts()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 6> temporalBindings{};
        temporalBindings[0] = {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};
        temporalBindings[1] = {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};
        temporalBindings[2] = {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};
        temporalBindings[3] = {3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        temporalBindings[4] = {4, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        temporalBindings[5] = {5, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute};

        vk::DescriptorSetLayoutCreateInfo temporalLayoutInfo{};
        temporalLayoutInfo.bindingCount = static_cast<uint32_t>(temporalBindings.size());
        temporalLayoutInfo.pBindings = temporalBindings.data();
        temporalDSLayout = vkDevice.createDescriptorSetLayout(temporalLayoutInfo);

        std::array<vk::DescriptorSetLayoutBinding, 4> spatialBindings{};
        spatialBindings[0] = {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};
        spatialBindings[1] = {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};
        spatialBindings[2] = {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        spatialBindings[3] = {3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};

        vk::DescriptorSetLayoutCreateInfo spatialLayoutInfo{};
        spatialLayoutInfo.bindingCount = static_cast<uint32_t>(spatialBindings.size());
        spatialLayoutInfo.pBindings = spatialBindings.data();
        spatialDSLayout = vkDevice.createDescriptorSetLayout(spatialLayoutInfo);

        vk::DescriptorSetLayoutBinding maskBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
                                                    vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo maskLayoutInfo{};
        maskLayoutInfo.bindingCount = 1;
        maskLayoutInfo.pBindings = &maskBinding;
        denoisedMaskSamplerLayout = vkDevice.createDescriptorSetLayout(maskLayoutInfo);
    }

    void RTLayeredShadowDenoiser::createDescriptorPools()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Temporal: MAX_SLICES * 2 ping-pong sets
        const uint32_t temporalSets = MAX_SLICES * 2;
        std::array<vk::DescriptorPoolSize, 3> temporalPoolSizes{};
        temporalPoolSizes[0] = {vk::DescriptorType::eStorageImage, 3 * temporalSets};
        temporalPoolSizes[1] = {vk::DescriptorType::eCombinedImageSampler, 2 * temporalSets};
        temporalPoolSizes[2] = {vk::DescriptorType::eUniformBuffer, temporalSets};

        vk::DescriptorPoolCreateInfo temporalPoolInfo{};
        temporalPoolInfo.maxSets = temporalSets;
        temporalPoolInfo.poolSizeCount = static_cast<uint32_t>(temporalPoolSizes.size());
        temporalPoolInfo.pPoolSizes = temporalPoolSizes.data();
        temporalDSPool = vkDevice.createDescriptorPool(temporalPoolInfo);

        // Spatial: per-frame * per-slice * per-pass
        const uint32_t spatialSets = core::MAX_FRAMES_IN_FLIGHT * MAX_SLICES * MAX_SPATIAL_PASSES;
        std::array<vk::DescriptorPoolSize, 2> spatialPoolSizes{};
        spatialPoolSizes[0] = {vk::DescriptorType::eStorageImage, 2 * spatialSets};
        spatialPoolSizes[1] = {vk::DescriptorType::eCombinedImageSampler, 2 * spatialSets};

        vk::DescriptorPoolCreateInfo spatialPoolInfo{};
        spatialPoolInfo.maxSets = spatialSets;
        spatialPoolInfo.poolSizeCount = static_cast<uint32_t>(spatialPoolSizes.size());
        spatialPoolInfo.pPoolSizes = spatialPoolSizes.data();
        spatialDSPool = vkDevice.createDescriptorPool(spatialPoolInfo);

        vk::DescriptorPoolSize maskPoolSize{vk::DescriptorType::eCombinedImageSampler, 1};
        vk::DescriptorPoolCreateInfo maskPoolInfo{};
        maskPoolInfo.maxSets = 1;
        maskPoolInfo.poolSizeCount = 1;
        maskPoolInfo.pPoolSizes = &maskPoolSize;
        denoisedMaskSamplerPool = vkDevice.createDescriptorPool(maskPoolInfo);
    }

    void RTLayeredShadowDenoiser::allocateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        for (uint32_t s = 0; s < MAX_SLICES; ++s)
        {
            std::array<vk::DescriptorSetLayout, 2> layouts = {temporalDSLayout, temporalDSLayout};
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = temporalDSPool;
            allocInfo.descriptorSetCount = 2;
            allocInfo.pSetLayouts = layouts.data();
            auto sets = vkDevice.allocateDescriptorSets(allocInfo);
            temporalDescSets[s][0] = sets[0];
            temporalDescSets[s][1] = sets[1];
        }

        for (uint32_t f = 0; f < core::MAX_FRAMES_IN_FLIGHT; ++f)
        {
            for (uint32_t s = 0; s < MAX_SLICES; ++s)
            {
                std::array<vk::DescriptorSetLayout, MAX_SPATIAL_PASSES> layouts;
                layouts.fill(spatialDSLayout);
                vk::DescriptorSetAllocateInfo allocInfo{};
                allocInfo.descriptorPool = spatialDSPool;
                allocInfo.descriptorSetCount = MAX_SPATIAL_PASSES;
                allocInfo.pSetLayouts = layouts.data();
                auto sets = vkDevice.allocateDescriptorSets(allocInfo);
                for (int p = 0; p < MAX_SPATIAL_PASSES; ++p)
                    spatialDescSets[f][s][p] = sets[p];
            }
        }

        vk::DescriptorSetAllocateInfo maskAllocInfo{};
        maskAllocInfo.descriptorPool = denoisedMaskSamplerPool;
        maskAllocInfo.descriptorSetCount = 1;
        maskAllocInfo.pSetLayouts = &denoisedMaskSamplerLayout;
        denoisedMaskSamplerDescSet = vkDevice.allocateDescriptorSets(maskAllocInfo)[0];
    }

    void RTLayeredShadowDenoiser::createTemporalPipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        temporalShader = std::make_unique<core::Shader>(device);
        temporalShader->readShader("../../resources/shaders/shadow/rt_shadow_temporal.glsl");
        if (temporalShader->getShaderStages().empty())
        {
            vfLogError("RTLayeredShadowDenoiser: Failed to compile temporal shader: {}", temporalShader->getLastCompilationError());
            return;
        }

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &temporalDSLayout;
        temporalPipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = temporalShader->getShaderStages()[0];
        pipelineInfo.layout = temporalPipelineLayout;
        temporalPipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
    }

    void RTLayeredShadowDenoiser::createSpatialPipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        spatialShader = std::make_unique<core::Shader>(device);
        spatialShader->readShader("../../resources/shaders/shadow/rt_shadow_spatial.glsl");
        if (spatialShader->getShaderStages().empty())
        {
            vfLogError("RTLayeredShadowDenoiser: Failed to compile spatial shader: {}", spatialShader->getLastCompilationError());
            return;
        }

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(ShadowSpatialPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &spatialDSLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        spatialPipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = spatialShader->getShaderStages()[0];
        pipelineInfo.layout = spatialPipelineLayout;
        spatialPipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
    }

    void RTLayeredShadowDenoiser::createDenoisedMaskSamplerDescriptor()
    {
        vk::DescriptorImageInfo maskInfo{};
        maskInfo.sampler = denoisedMaskSampler;
        maskInfo.imageView = denoisedOutputArraySampledView;
        maskInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = denoisedMaskSamplerDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &maskInfo;

        device.getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
    }

    void RTLayeredShadowDenoiser::dispatch(vk::CommandBuffer cmd,
                                           const std::vector<RTLayeredDenoiseInfo>& slices,
                                           vk::ImageView depthView,
                                           vk::Image depthImage,
                                           vk::ImageView normalView,
                                           vk::Image normalImage,
                                           const glm::mat4& invViewProjection,
                                           const glm::mat4& viewProjection,
                                           uint32_t screenWidth,
                                           uint32_t screenHeight,
                                           uint32_t frameIndex,
                                           uint32_t resourceFrameIndex)
    {
        if (!initialized || !temporalPipeline || !spatialPipeline || slices.empty()) return;

        uint32_t fi = resourceFrameIndex % core::MAX_FRAMES_IN_FLIGHT;
        paramsBuffer.setFrame(fi);

        const bool anyHistory = spatialImagesReady; // becomes true after the first denoise frame

        ShadowDenoiserUBO ubo{};
        ubo.invViewProjection = invViewProjection;
        ubo.prevViewProjection = anyHistory ? prevViewProjection : glm::mat4(1.0f);
        ubo.screenParams = glm::vec4(
            static_cast<float>(screenWidth), static_cast<float>(screenHeight),
            1.0f / static_cast<float>(screenWidth), 1.0f / static_cast<float>(screenHeight));
        ubo.temporalParams = glm::vec4(
            anyHistory ? temporalBlend : 0.0f,
            static_cast<float>(frameIndex),
            depthThreshold,
            normalThreshold);
        paramsBuffer.write(&ubo, sizeof(ShadowDenoiserUBO));

        uint32_t readIdx = 1 - currentHistoryIndex;
        uint32_t writeIdx = currentHistoryIndex;

        const uint32_t groupsX = (screenWidth + 7) / 8;
        const uint32_t groupsY = (screenHeight + 7) / 8;

        vk::MemoryBarrier memBarrier{};
        memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        // Transition both history arrays (all layers) to general the first time.
        if (!historyImagesInGeneral)
        {
            for (int i = 0; i < 2; ++i)
            {
                vk::ImageMemoryBarrier barrier{};
                barrier.srcAccessMask = {};
                barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = vk::ImageLayout::eUndefined;
                barrier.newLayout = vk::ImageLayout::eGeneral;
                barrier.image = history[i].image;
                barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, MAX_SLICES};
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            historyImagesInGeneral = true;
        }

        // Reset history for freshly (re)assigned slices: clear their read+write layers to 0 so the
        // previous owner's shadow doesn't bleed in via reprojection. Batched before the slice loop.
        bool anyReset = false;
        for (const RTLayeredDenoiseInfo& s : slices)
            if (s.resetHistory && s.slice < MAX_SLICES) { anyReset = true; break; }

        if (anyReset)
        {
            // Ensure the layout is settled before the transfer clear.
            vk::ImageMemoryBarrier pre{};
            pre.srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
            pre.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            pre.oldLayout = vk::ImageLayout::eGeneral;
            pre.newLayout = vk::ImageLayout::eGeneral;
            pre.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, MAX_SLICES};
            std::array<vk::ImageMemoryBarrier, 2> preBarriers{pre, pre};
            preBarriers[0].image = history[0].image;
            preBarriers[1].image = history[1].image;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eTransfer,
                {}, 0, nullptr, 0, nullptr, 2, preBarriers.data());

            vk::ClearColorValue clearColor{};
            clearColor.setFloat32({0.0f, 0.0f, 0.0f, 0.0f});
            for (const RTLayeredDenoiseInfo& s : slices)
            {
                if (!s.resetHistory || s.slice >= MAX_SLICES) continue;
                vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, s.slice, 1};
                cmd.clearColorImage(history[0].image, vk::ImageLayout::eGeneral, clearColor, range);
                cmd.clearColorImage(history[1].image, vk::ImageLayout::eGeneral, clearColor, range);
            }

            vk::ImageMemoryBarrier post = pre;
            post.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            post.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
            std::array<vk::ImageMemoryBarrier, 2> postBarriers{post, post};
            postBarriers[0].image = history[0].image;
            postBarriers[1].image = history[1].image;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, 0, nullptr, 0, nullptr, 2, postBarriers.data());
        }

        // Transition spatial scratch + denoised output (all layers) to general.
        {
            auto toGeneral = [&](vk::Image image, vk::ImageLayout oldLayout, uint32_t layers) {
                vk::ImageMemoryBarrier barrier{};
                barrier.srcAccessMask = (oldLayout == vk::ImageLayout::eUndefined) ? vk::AccessFlags{} : vk::AccessFlagBits::eShaderRead;
                barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = oldLayout;
                barrier.newLayout = vk::ImageLayout::eGeneral;
                barrier.image = image;
                barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, layers};
                cmd.pipelineBarrier(
                    (oldLayout == vk::ImageLayout::eUndefined) ? vk::PipelineStageFlagBits::eTopOfPipe : vk::PipelineStageFlagBits::eFragmentShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            };
            vk::ImageLayout spatialOld = spatialImagesReady ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined;
            for (int i = 0; i < 2; ++i)
                toGeneral(spatialBuf[i].image, spatialOld, 1);
            vk::ImageLayout outputOld = spatialImagesReady ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined;
            toGeneral(denoisedOutputImage, outputOld, MAX_SLICES);
            spatialImagesReady = true;
        }

        const int stepSizes[MAX_SPATIAL_PASSES] = {1, 2, 4, 8, 16};
        const int numPasses = std::clamp(spatialPassCount, 1, MAX_SPATIAL_PASSES);

        for (const RTLayeredDenoiseInfo& info : slices)
        {
            const uint32_t slice = info.slice;
            if (slice >= MAX_SLICES) continue;

            // ===== TEMPORAL PASS (this slice's history layer) =====
            {
                std::array<vk::WriteDescriptorSet, 6> writes{};

                vk::DescriptorImageInfo rawInfo{};
                rawInfo.imageView = info.rawLayerView;
                rawInfo.imageLayout = vk::ImageLayout::eGeneral;
                writes[0].dstSet = temporalDescSets[slice][writeIdx];
                writes[0].dstBinding = 0;
                writes[0].descriptorCount = 1;
                writes[0].descriptorType = vk::DescriptorType::eStorageImage;
                writes[0].pImageInfo = &rawInfo;

                vk::DescriptorImageInfo histReadInfo{};
                histReadInfo.imageView = history[readIdx].layerView[slice];
                histReadInfo.imageLayout = vk::ImageLayout::eGeneral;
                writes[1].dstSet = temporalDescSets[slice][writeIdx];
                writes[1].dstBinding = 1;
                writes[1].descriptorCount = 1;
                writes[1].descriptorType = vk::DescriptorType::eStorageImage;
                writes[1].pImageInfo = &histReadInfo;

                vk::DescriptorImageInfo histWriteInfo{};
                histWriteInfo.imageView = history[writeIdx].layerView[slice];
                histWriteInfo.imageLayout = vk::ImageLayout::eGeneral;
                writes[2].dstSet = temporalDescSets[slice][writeIdx];
                writes[2].dstBinding = 2;
                writes[2].descriptorCount = 1;
                writes[2].descriptorType = vk::DescriptorType::eStorageImage;
                writes[2].pImageInfo = &histWriteInfo;

                vk::DescriptorImageInfo depthInfo{};
                depthInfo.sampler = nearestSampler;
                depthInfo.imageView = depthView;
                depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                writes[3].dstSet = temporalDescSets[slice][writeIdx];
                writes[3].dstBinding = 3;
                writes[3].descriptorCount = 1;
                writes[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
                writes[3].pImageInfo = &depthInfo;

                vk::DescriptorImageInfo normalInfo{};
                normalInfo.sampler = nearestSampler;
                normalInfo.imageView = normalView;
                normalInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                writes[4].dstSet = temporalDescSets[slice][writeIdx];
                writes[4].dstBinding = 4;
                writes[4].descriptorCount = 1;
                writes[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
                writes[4].pImageInfo = &normalInfo;

                vk::DescriptorBufferInfo paramsInfo{};
                paramsInfo.buffer = paramsBuffer.getBuffer();
                paramsInfo.offset = 0;
                paramsInfo.range = sizeof(ShadowDenoiserUBO);
                writes[5].dstSet = temporalDescSets[slice][writeIdx];
                writes[5].dstBinding = 5;
                writes[5].descriptorCount = 1;
                writes[5].descriptorType = vk::DescriptorType::eUniformBuffer;
                writes[5].pBufferInfo = &paramsInfo;

                device.getLogicalDevice().updateDescriptorSets(
                    static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }

            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, temporalPipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, temporalPipelineLayout,
                                   0, 1, &temporalDescSets[slice][writeIdx], 0, nullptr);
            cmd.dispatch(groupsX, groupsY, 1);

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, 1, &memBarrier, 0, nullptr, 0, nullptr);

            // ===== SPATIAL PASSES (history[writeIdx][slice] -> ... -> denoisedOutput[slice]) =====
            auto getSpatialInput = [&](int pass) -> vk::ImageView {
                if (pass == 0) return history[writeIdx].layerView[slice];
                return spatialBuf[(pass - 1) % 2].storageView;
            };
            auto getSpatialOutput = [&](int pass) -> vk::ImageView {
                if (pass == numPasses - 1) return denoisedOutputLayerView[slice];
                return spatialBuf[pass % 2].storageView;
            };

            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, spatialPipeline);

            for (int pass = 0; pass < numPasses; ++pass)
            {
                {
                    std::array<vk::WriteDescriptorSet, 4> writes{};

                    vk::DescriptorImageInfo inputInfo{};
                    inputInfo.imageView = getSpatialInput(pass);
                    inputInfo.imageLayout = vk::ImageLayout::eGeneral;
                    writes[0].dstSet = spatialDescSets[fi][slice][pass];
                    writes[0].dstBinding = 0;
                    writes[0].descriptorCount = 1;
                    writes[0].descriptorType = vk::DescriptorType::eStorageImage;
                    writes[0].pImageInfo = &inputInfo;

                    vk::DescriptorImageInfo outputInfo{};
                    outputInfo.imageView = getSpatialOutput(pass);
                    outputInfo.imageLayout = vk::ImageLayout::eGeneral;
                    writes[1].dstSet = spatialDescSets[fi][slice][pass];
                    writes[1].dstBinding = 1;
                    writes[1].descriptorCount = 1;
                    writes[1].descriptorType = vk::DescriptorType::eStorageImage;
                    writes[1].pImageInfo = &outputInfo;

                    vk::DescriptorImageInfo depthInfo{};
                    depthInfo.sampler = nearestSampler;
                    depthInfo.imageView = depthView;
                    depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    writes[2].dstSet = spatialDescSets[fi][slice][pass];
                    writes[2].dstBinding = 2;
                    writes[2].descriptorCount = 1;
                    writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
                    writes[2].pImageInfo = &depthInfo;

                    vk::DescriptorImageInfo normalInfo{};
                    normalInfo.sampler = nearestSampler;
                    normalInfo.imageView = normalView;
                    normalInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    writes[3].dstSet = spatialDescSets[fi][slice][pass];
                    writes[3].dstBinding = 3;
                    writes[3].descriptorCount = 1;
                    writes[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
                    writes[3].pImageInfo = &normalInfo;

                    device.getLogicalDevice().updateDescriptorSets(
                        static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
                }

                cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, spatialPipelineLayout,
                                       0, 1, &spatialDescSets[fi][slice][pass], 0, nullptr);

                ShadowSpatialPushConstants pc{};
                pc.stepSize = stepSizes[pass];
                pc.phiDepth = spatialPhiDepth;
                pc.phiNormal = spatialPhiNormal;
                pc.passIndex = static_cast<uint32_t>(pass);
                cmd.pushConstants(spatialPipelineLayout, vk::ShaderStageFlagBits::eCompute,
                                  0, sizeof(ShadowSpatialPushConstants), &pc);

                cmd.dispatch(groupsX, groupsY, 1);

                // Barrier between passes, and between slices (last pass -> next slice's temporal).
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 1, &memBarrier, 0, nullptr, 0, nullptr);
            }
        }

        // ===== FINAL TRANSITIONS =====
        core::ImageUtilities::transitionImageLayout(cmd, denoisedOutputImage,
            vk::ImageLayout::eGeneral,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, MAX_SLICES);

        core::ImageUtilities::transitionImageLayout(cmd, depthImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageAspectFlagBits::eDepth);
        core::ImageUtilities::transitionImageLayout(cmd, normalImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        currentHistoryIndex = 1 - currentHistoryIndex;
        prevViewProjection = viewProjection;
    }
}
