#include "RTShadowDenoiser.hpp"
#include <algorithm>
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::raytracing
{
    RTShadowDenoiser::RTShadowDenoiser(core::Device& device)
        : device(device)
    {
    }

    RTShadowDenoiser::~RTShadowDenoiser()
    {
        cleanup();
    }

    void RTShadowDenoiser::init(uint32_t width, uint32_t height)
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
        historyValid = false;
        vfLogInfo("RTShadowDenoiser: Initialized {}x{}", width, height);
    }

    void RTShadowDenoiser::cleanup()
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

    void RTShadowDenoiser::resize(uint32_t width, uint32_t height)
    {
        if (!initialized) return;
        if (width == maskWidth && height == maskHeight) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        destroyImages();
        createImages(width, height);

        // Re-update all descriptor sets with new image views
        // Temporal desc sets will be updated dynamically in dispatch
        // Spatial desc sets will be updated dynamically in dispatch
        createDenoisedMaskSamplerDescriptor();

        historyValid = false;
        spatialImagesReady = false;
        vfLogInfo("RTShadowDenoiser: Resized to {}x{}", width, height);
    }

    void RTShadowDenoiser::createImages(uint32_t w, uint32_t h)
    {
        maskWidth = w;
        maskHeight = h;

        // History ping-pong (R16Sfloat)
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
            core::ImageUtilities::createImage(request, history[i].image, history[i].allocation, device.getMemoryManager());

            core::ImageViewInfoRequest viewReq(
                device.getLogicalDevice(), history[i].image,
                vk::Format::eR16Sfloat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2D, 1, 1
            );
            core::ImageUtilities::createImageView(viewReq, history[i].storageView);
        }

        // Spatial ping-pong (R16Sfloat)
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

        // Denoised output (R16Sfloat — matches spatial shader format, sampler handles conversion)
        {
            core::ImageInfoRequest request(
                device.getLogicalDevice(), device.getPhysicalDevice(),
                w, h, 1, 1,
                vk::Format::eR16Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            );
            core::ImageUtilities::createImage(request, denoisedOutputImage, denoisedOutputAllocation, device.getMemoryManager());

            core::ImageViewInfoRequest storageViewReq(
                device.getLogicalDevice(), denoisedOutputImage,
                vk::Format::eR16Sfloat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2D, 1, 1
            );
            core::ImageUtilities::createImageView(storageViewReq, denoisedOutputStorageView);

            core::ImageViewInfoRequest sampledViewReq(
                device.getLogicalDevice(), denoisedOutputImage,
                vk::Format::eR16Sfloat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2D, 1, 1
            );
            core::ImageUtilities::createImageView(sampledViewReq, denoisedOutputSampledView);
        }
    }

    void RTShadowDenoiser::destroyImages()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        for (int i = 0; i < 2; ++i)
        {
            vkDevice.destroyImageView(history[i].storageView);
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

        vkDevice.destroyImageView(denoisedOutputStorageView);
        vkDevice.destroyImageView(denoisedOutputSampledView);
        vkDevice.destroyImage(denoisedOutputImage);
        device.getMemoryManager().free(denoisedOutputAllocation);
        denoisedOutputAllocation = {};
    }

    void RTShadowDenoiser::createSamplers()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        nearestSampler = vkDevice.createSampler(samplerInfo);

        // Linear sampler for denoised output fragment reads
        vk::SamplerCreateInfo linearInfo = samplerInfo;
        linearInfo.magFilter = vk::Filter::eLinear;
        linearInfo.minFilter = vk::Filter::eLinear;
        denoisedMaskSampler = vkDevice.createSampler(linearInfo);
    }

    void RTShadowDenoiser::createParamsBuffer()
    {
        paramsBuffer.create(device.getLogicalDevice(), device.getPhysicalDevice(),
                            sizeof(ShadowDenoiserUBO), device.getMemoryManager());
    }

    void RTShadowDenoiser::createDescriptorLayouts()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Temporal layout: 3 storage images + 2 samplers + 1 UBO
        std::array<vk::DescriptorSetLayoutBinding, 6> temporalBindings{};
        temporalBindings[0] = {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};  // raw shadow
        temporalBindings[1] = {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};  // history read
        temporalBindings[2] = {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};  // history write
        temporalBindings[3] = {3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute}; // depth
        temporalBindings[4] = {4, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute}; // normal
        temporalBindings[5] = {5, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute};        // UBO

        vk::DescriptorSetLayoutCreateInfo temporalLayoutInfo{};
        temporalLayoutInfo.bindingCount = static_cast<uint32_t>(temporalBindings.size());
        temporalLayoutInfo.pBindings = temporalBindings.data();
        temporalDSLayout = vkDevice.createDescriptorSetLayout(temporalLayoutInfo);

        // Spatial layout: 2 storage images + 2 samplers
        std::array<vk::DescriptorSetLayoutBinding, 4> spatialBindings{};
        spatialBindings[0] = {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};  // input
        spatialBindings[1] = {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};  // output
        spatialBindings[2] = {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute}; // depth
        spatialBindings[3] = {3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute}; // normal

        vk::DescriptorSetLayoutCreateInfo spatialLayoutInfo{};
        spatialLayoutInfo.bindingCount = static_cast<uint32_t>(spatialBindings.size());
        spatialLayoutInfo.pBindings = spatialBindings.data();
        spatialDSLayout = vkDevice.createDescriptorSetLayout(spatialLayoutInfo);

        // Denoised mask sampler layout (for fragment shader, set 13)
        vk::DescriptorSetLayoutBinding maskBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
                                                    vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo maskLayoutInfo{};
        maskLayoutInfo.bindingCount = 1;
        maskLayoutInfo.pBindings = &maskBinding;
        denoisedMaskSamplerLayout = vkDevice.createDescriptorSetLayout(maskLayoutInfo);
    }

    void RTShadowDenoiser::createDescriptorPools()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Temporal pool: 2 sets (ping-pong)
        std::array<vk::DescriptorPoolSize, 3> temporalPoolSizes{};
        temporalPoolSizes[0] = {vk::DescriptorType::eStorageImage, 6};           // 3 per set * 2
        temporalPoolSizes[1] = {vk::DescriptorType::eCombinedImageSampler, 4};   // 2 per set * 2
        temporalPoolSizes[2] = {vk::DescriptorType::eUniformBuffer, 2};          // 1 per set * 2

        vk::DescriptorPoolCreateInfo temporalPoolInfo{};
        temporalPoolInfo.maxSets = 2;
        temporalPoolInfo.poolSizeCount = static_cast<uint32_t>(temporalPoolSizes.size());
        temporalPoolInfo.pPoolSizes = temporalPoolSizes.data();
        temporalDSPool = vkDevice.createDescriptorPool(temporalPoolInfo);

        // Spatial pool: per-frame sets to avoid descriptor update races
        uint32_t totalSpatialSets = MAX_SPATIAL_PASSES * core::MAX_FRAMES_IN_FLIGHT;
        std::array<vk::DescriptorPoolSize, 2> spatialPoolSizes{};
        spatialPoolSizes[0] = {vk::DescriptorType::eStorageImage, 2 * totalSpatialSets};
        spatialPoolSizes[1] = {vk::DescriptorType::eCombinedImageSampler, 2 * totalSpatialSets};

        vk::DescriptorPoolCreateInfo spatialPoolInfo{};
        spatialPoolInfo.maxSets = totalSpatialSets;
        spatialPoolInfo.poolSizeCount = static_cast<uint32_t>(spatialPoolSizes.size());
        spatialPoolInfo.pPoolSizes = spatialPoolSizes.data();
        spatialDSPool = vkDevice.createDescriptorPool(spatialPoolInfo);

        // Denoised mask sampler pool
        vk::DescriptorPoolSize maskPoolSize{vk::DescriptorType::eCombinedImageSampler, 1};
        vk::DescriptorPoolCreateInfo maskPoolInfo{};
        maskPoolInfo.maxSets = 1;
        maskPoolInfo.poolSizeCount = 1;
        maskPoolInfo.pPoolSizes = &maskPoolSize;
        denoisedMaskSamplerPool = vkDevice.createDescriptorPool(maskPoolInfo);
    }

    void RTShadowDenoiser::allocateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Temporal: 2 sets
        std::array<vk::DescriptorSetLayout, 2> temporalLayouts = {temporalDSLayout, temporalDSLayout};
        vk::DescriptorSetAllocateInfo temporalAllocInfo{};
        temporalAllocInfo.descriptorPool = temporalDSPool;
        temporalAllocInfo.descriptorSetCount = 2;
        temporalAllocInfo.pSetLayouts = temporalLayouts.data();
        auto temporalSets = vkDevice.allocateDescriptorSets(temporalAllocInfo);
        temporalDescSets[0] = temporalSets[0];
        temporalDescSets[1] = temporalSets[1];

        // Spatial: per-frame sets to avoid descriptor update races
        for (uint32_t f = 0; f < core::MAX_FRAMES_IN_FLIGHT; ++f)
        {
            std::array<vk::DescriptorSetLayout, MAX_SPATIAL_PASSES> spatialLayouts;
            spatialLayouts.fill(spatialDSLayout);
            vk::DescriptorSetAllocateInfo spatialAllocInfo{};
            spatialAllocInfo.descriptorPool = spatialDSPool;
            spatialAllocInfo.descriptorSetCount = MAX_SPATIAL_PASSES;
            spatialAllocInfo.pSetLayouts = spatialLayouts.data();
            auto spatialSets = vkDevice.allocateDescriptorSets(spatialAllocInfo);
            for (int i = 0; i < MAX_SPATIAL_PASSES; ++i)
                spatialDescSets[f][i] = spatialSets[i];
        }

        // Denoised mask sampler set
        vk::DescriptorSetAllocateInfo maskAllocInfo{};
        maskAllocInfo.descriptorPool = denoisedMaskSamplerPool;
        maskAllocInfo.descriptorSetCount = 1;
        maskAllocInfo.pSetLayouts = &denoisedMaskSamplerLayout;
        denoisedMaskSamplerDescSet = vkDevice.allocateDescriptorSets(maskAllocInfo)[0];
    }

    void RTShadowDenoiser::createTemporalPipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        temporalShader = std::make_unique<core::Shader>(device);
        temporalShader->readShader("../../resources/shaders/shadow/rt_shadow_temporal.glsl");

        if (temporalShader->getShaderStages().empty())
        {
            vfLogError("RTShadowDenoiser: Failed to compile temporal shader: {}", temporalShader->getLastCompilationError());
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

    void RTShadowDenoiser::createSpatialPipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        spatialShader = std::make_unique<core::Shader>(device);
        spatialShader->readShader("../../resources/shaders/shadow/rt_shadow_spatial.glsl");

        if (spatialShader->getShaderStages().empty())
        {
            vfLogError("RTShadowDenoiser: Failed to compile spatial shader: {}", spatialShader->getLastCompilationError());
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

    void RTShadowDenoiser::createDenoisedMaskSamplerDescriptor()
    {
        vk::DescriptorImageInfo maskInfo{};
        maskInfo.sampler = denoisedMaskSampler;
        maskInfo.imageView = denoisedOutputSampledView;
        maskInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = denoisedMaskSamplerDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &maskInfo;

        device.getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
    }

    void RTShadowDenoiser::dispatch(vk::CommandBuffer cmd,
                                     vk::ImageView rawShadowView,
                                     vk::Image rawShadowImage,
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
        if (!initialized || !temporalPipeline || !spatialPipeline) return;

        // Use caller-provided frame index aligned with OffScreenViewPort fence scheme
        uint32_t fi = resourceFrameIndex % core::MAX_FRAMES_IN_FLIGHT;
        paramsBuffer.setFrame(fi);

        // Update UBO for this frame slot
        ShadowDenoiserUBO ubo{};
        ubo.invViewProjection = invViewProjection;
        ubo.prevViewProjection = historyValid ? prevViewProjection : glm::mat4(1.0f);
        ubo.screenParams = glm::vec4(
            static_cast<float>(screenWidth), static_cast<float>(screenHeight),
            1.0f / static_cast<float>(screenWidth), 1.0f / static_cast<float>(screenHeight));
        ubo.temporalParams = glm::vec4(
            historyValid ? temporalBlend : 0.0f,
            static_cast<float>(frameIndex),
            depthThreshold,
            normalThreshold
        );
        paramsBuffer.write(&ubo, sizeof(ShadowDenoiserUBO));

        // Determine ping-pong: read from prev history, write to current
        uint32_t readIdx = 1 - currentHistoryIndex;
        uint32_t writeIdx = currentHistoryIndex;

        // Update temporal descriptor set for current ping-pong config
        {
            std::array<vk::WriteDescriptorSet, 6> writes{};

            // Binding 0: raw shadow mask (storage image, read)
            vk::DescriptorImageInfo rawInfo{};
            rawInfo.imageView = rawShadowView;
            rawInfo.imageLayout = vk::ImageLayout::eGeneral;
            writes[0].dstSet = temporalDescSets[writeIdx];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eStorageImage;
            writes[0].pImageInfo = &rawInfo;

            // Binding 1: history read
            vk::DescriptorImageInfo histReadInfo{};
            histReadInfo.imageView = history[readIdx].storageView;
            histReadInfo.imageLayout = vk::ImageLayout::eGeneral;
            writes[1].dstSet = temporalDescSets[writeIdx];
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eStorageImage;
            writes[1].pImageInfo = &histReadInfo;

            // Binding 2: history write
            vk::DescriptorImageInfo histWriteInfo{};
            histWriteInfo.imageView = history[writeIdx].storageView;
            histWriteInfo.imageLayout = vk::ImageLayout::eGeneral;
            writes[2].dstSet = temporalDescSets[writeIdx];
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = vk::DescriptorType::eStorageImage;
            writes[2].pImageInfo = &histWriteInfo;

            // Binding 3: depth
            vk::DescriptorImageInfo depthInfo{};
            depthInfo.sampler = nearestSampler;
            depthInfo.imageView = depthView;
            depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            writes[3].dstSet = temporalDescSets[writeIdx];
            writes[3].dstBinding = 3;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[3].pImageInfo = &depthInfo;

            // Binding 4: normal
            vk::DescriptorImageInfo normalInfo{};
            normalInfo.sampler = nearestSampler;
            normalInfo.imageView = normalView;
            normalInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            writes[4].dstSet = temporalDescSets[writeIdx];
            writes[4].dstBinding = 4;
            writes[4].descriptorCount = 1;
            writes[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[4].pImageInfo = &normalInfo;

            // Binding 5: UBO (per-frame buffer)
            vk::DescriptorBufferInfo paramsInfo{};
            paramsInfo.buffer = paramsBuffer.getBuffer();
            paramsInfo.offset = 0;
            paramsInfo.range = sizeof(ShadowDenoiserUBO);
            writes[5].dstSet = temporalDescSets[writeIdx];
            writes[5].dstBinding = 5;
            writes[5].descriptorCount = 1;
            writes[5].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[5].pBufferInfo = &paramsInfo;

            device.getLogicalDevice().updateDescriptorSets(
                static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        // Transition history images to eGeneral
        if (!historyValid)
        {
            for (int i = 0; i < 2; ++i)
            {
                vk::ImageMemoryBarrier barrier{};
                barrier.srcAccessMask = {};
                barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = vk::ImageLayout::eUndefined;
                barrier.newLayout = vk::ImageLayout::eGeneral;
                barrier.image = history[i].image;
                barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            }
        }

        // === TEMPORAL PASS ===
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, temporalPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, temporalPipelineLayout,
                               0, 1, &temporalDescSets[writeIdx], 0, nullptr);

        uint32_t groupsX = (screenWidth + 7) / 8;
        uint32_t groupsY = (screenHeight + 7) / 8;
        cmd.dispatch(groupsX, groupsY, 1);

        // Barrier: temporal write -> spatial read
        vk::MemoryBarrier memBarrier{};
        memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, 1, &memBarrier, 0, nullptr, 0, nullptr);

        // === SPATIAL PASSES (3 à-trous iterations) ===
        // Pass 0: history[writeIdx] -> spatialBuf[0]
        // Pass 1: spatialBuf[0] -> spatialBuf[1]
        // Pass 2: spatialBuf[1] -> denoisedOutput (R16Sfloat)

        // Transition spatial buffers and denoised output to eGeneral
        {
            auto toGeneral = [&](vk::Image image, vk::ImageLayout oldLayout) {
                vk::ImageMemoryBarrier barrier{};
                barrier.srcAccessMask = (oldLayout == vk::ImageLayout::eUndefined) ? vk::AccessFlags{} : vk::AccessFlagBits::eShaderRead;
                barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = oldLayout;
                barrier.newLayout = vk::ImageLayout::eGeneral;
                barrier.image = image;
                barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
                cmd.pipelineBarrier(
                    (oldLayout == vk::ImageLayout::eUndefined) ? vk::PipelineStageFlagBits::eTopOfPipe : vk::PipelineStageFlagBits::eFragmentShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            };
            // Spatial ping-pong: undefined on first frame, eGeneral after
            vk::ImageLayout spatialOld = spatialImagesReady ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined;
            for (int i = 0; i < 2; ++i)
                toGeneral(spatialBuf[i].image, spatialOld);
            // Denoised output: undefined on first frame, eShaderReadOnlyOptimal after (fragment shader reads it)
            vk::ImageLayout outputOld = spatialImagesReady ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined;
            toGeneral(denoisedOutputImage, outputOld);
            spatialImagesReady = true;
        }

        const int stepSizes[MAX_SPATIAL_PASSES] = {1, 2, 4, 8, 16};
        int numPasses = std::clamp(spatialPassCount, 1, MAX_SPATIAL_PASSES);

        // Build input/output view arrays dynamically
        // Pattern: temporal → buf[0] → buf[1] → buf[0] → ... → denoisedOutput
        auto getSpatialInput = [&](int pass) -> vk::ImageView {
            if (pass == 0) return history[writeIdx].storageView;
            return spatialBuf[(pass - 1) % 2].storageView;
        };
        auto getSpatialOutput = [&](int pass) -> vk::ImageView {
            if (pass == numPasses - 1) return denoisedOutputStorageView;
            return spatialBuf[pass % 2].storageView;
        };

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, spatialPipeline);

        for (int pass = 0; pass < numPasses; ++pass)
        {
            // Update spatial descriptor set for this pass
            {
                std::array<vk::WriteDescriptorSet, 4> writes{};

                vk::DescriptorImageInfo inputInfo{};
                inputInfo.imageView = getSpatialInput(pass);
                inputInfo.imageLayout = vk::ImageLayout::eGeneral;
                writes[0].dstSet = spatialDescSets[fi][pass];
                writes[0].dstBinding = 0;
                writes[0].descriptorCount = 1;
                writes[0].descriptorType = vk::DescriptorType::eStorageImage;
                writes[0].pImageInfo = &inputInfo;

                vk::DescriptorImageInfo outputInfo{};
                outputInfo.imageView = getSpatialOutput(pass);
                outputInfo.imageLayout = vk::ImageLayout::eGeneral;
                writes[1].dstSet = spatialDescSets[fi][pass];
                writes[1].dstBinding = 1;
                writes[1].descriptorCount = 1;
                writes[1].descriptorType = vk::DescriptorType::eStorageImage;
                writes[1].pImageInfo = &outputInfo;

                vk::DescriptorImageInfo depthInfo{};
                depthInfo.sampler = nearestSampler;
                depthInfo.imageView = depthView;
                depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                writes[2].dstSet = spatialDescSets[fi][pass];
                writes[2].dstBinding = 2;
                writes[2].descriptorCount = 1;
                writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
                writes[2].pImageInfo = &depthInfo;

                vk::DescriptorImageInfo normalInfo{};
                normalInfo.sampler = nearestSampler;
                normalInfo.imageView = normalView;
                normalInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                writes[3].dstSet = spatialDescSets[fi][pass];
                writes[3].dstBinding = 3;
                writes[3].descriptorCount = 1;
                writes[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
                writes[3].pImageInfo = &normalInfo;

                device.getLogicalDevice().updateDescriptorSets(
                    static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, spatialPipelineLayout,
                                   0, 1, &spatialDescSets[fi][pass], 0, nullptr);

            ShadowSpatialPushConstants pc{};
            pc.stepSize = stepSizes[pass];
            pc.phiDepth = spatialPhiDepth;
            pc.phiNormal = spatialPhiNormal;
            pc.passIndex = static_cast<uint32_t>(pass);
            cmd.pushConstants(spatialPipelineLayout, vk::ShaderStageFlagBits::eCompute,
                              0, sizeof(ShadowSpatialPushConstants), &pc);

            cmd.dispatch(groupsX, groupsY, 1);

            // Barrier between spatial passes
            if (pass < numPasses - 1)
            {
                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 1, &memBarrier, 0, nullptr, 0, nullptr);
            }
        }

        // === FINAL TRANSITIONS ===

        // Denoised output: general -> shader read (for fragment shader)
        core::ImageUtilities::transitionImageLayout(cmd, denoisedOutputImage,
            vk::ImageLayout::eGeneral,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Depth: shader read -> attachment
        core::ImageUtilities::transitionImageLayout(cmd, depthImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageAspectFlagBits::eDepth);

        // Normal: shader read -> color attachment
        core::ImageUtilities::transitionImageLayout(cmd, normalImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Flip history ping-pong for next frame
        currentHistoryIndex = 1 - currentHistoryIndex;
        prevViewProjection = viewProjection;
        historyValid = true;
    }
}
