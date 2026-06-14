#include "RTPointShadowPipeline.hpp"
#include "RTShadowPipeline.hpp" // RTShadowParams (shared input UBO layout)
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
    RTPointShadowPipeline::RTPointShadowPipeline(core::Device& device)
        : device(device)
    {
    }

    RTPointShadowPipeline::~RTPointShadowPipeline()
    {
        cleanup();
    }

    void RTPointShadowPipeline::init(uint32_t width, uint32_t height, vk::DescriptorSetLayout tlasLayout)
    {
        if (initialized) return;
        if (!device.isRayQuerySupported())
        {
            vfLogWarning("RTPointShadowPipeline: Ray query not supported");
            return;
        }

        maskWidth = width;
        maskHeight = height;
        cachedTlasLayout = tlasLayout;

        createShadowMaskImage(width, height);
        createSamplers();
        createParamsBuffer();
        createDescriptorLayouts(tlasLayout);
        createDescriptorPool();
        allocateDescriptorSets();
        createComputePipeline();
        updateOutputDescriptors();
        createShadowMaskSamplerDescriptor();

        initialized = true;
        firstFrame = true;
        vfLogInfo("RTPointShadowPipeline: Initialized {}x{} x{} slices", width, height, MAX_SLICES);
    }

    void RTPointShadowPipeline::cleanup()
    {
        if (!initialized) return;
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        vkDevice.destroyPipeline(computePipeline);
        vkDevice.destroyPipelineLayout(pipelineLayout);

        vkDevice.destroyDescriptorPool(descriptorPool);
        vkDevice.destroyDescriptorPool(shadowMaskSamplerPool);
        vkDevice.destroyDescriptorSetLayout(inputLayout);
        vkDevice.destroyDescriptorSetLayout(outputLayout);
        vkDevice.destroyDescriptorSetLayout(shadowMaskSamplerLayout);

        vkDevice.destroySampler(depthSampler);
        vkDevice.destroySampler(normalSampler);
        vkDevice.destroySampler(shadowMaskSampler);

        paramsBuffer.destroy(vkDevice, device.getMemoryManager());

        destroyShadowMaskImage();

        shader.reset();
        initialized = false;
    }

    bool RTPointShadowPipeline::resize(uint32_t width, uint32_t height)
    {
        if (!initialized) return false;
        if (width == maskWidth && height == maskHeight) return false;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        destroyShadowMaskImage();
        createShadowMaskImage(width, height);
        updateOutputDescriptors();
        createShadowMaskSamplerDescriptor();
        firstFrame = true;

        vfLogInfo("RTPointShadowPipeline: Resized to {}x{}", width, height);
        return true;
    }

    void RTPointShadowPipeline::createShadowMaskImage(uint32_t w, uint32_t h)
    {
        maskWidth = w;
        maskHeight = h;

        core::ImageInfoRequest request(
            device.getLogicalDevice(), device.getPhysicalDevice(),
            w, h, MAX_SLICES, 1,
            vk::Format::eR8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(request, shadowMaskImage, shadowMaskAllocation, device.getMemoryManager());

        vk::Device vkDevice = device.getLogicalDevice();

        // Per-layer storage views (e2D, baseArrayLayer = k) for compute write — the helper only
        // creates baseArrayLayer = 0 views, so build these directly.
        for (uint32_t k = 0; k < MAX_SLICES; ++k)
        {
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = shadowMaskImage;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = vk::Format::eR8Unorm;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = k;
            viewInfo.subresourceRange.layerCount = 1;
            shadowMaskLayerStorageViews[k] = vkDevice.createImageView(viewInfo);
        }

        // Array sampled view (e2DArray) for fragment read.
        core::ImageViewInfoRequest arrayViewReq(
            device.getLogicalDevice(), shadowMaskImage,
            vk::Format::eR8Unorm, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2DArray, MAX_SLICES, 1
        );
        core::ImageUtilities::createImageView(arrayViewReq, shadowMaskArraySampledView);
    }

    void RTPointShadowPipeline::destroyShadowMaskImage()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        for (uint32_t k = 0; k < MAX_SLICES; ++k)
        {
            vkDevice.destroyImageView(shadowMaskLayerStorageViews[k]);
            shadowMaskLayerStorageViews[k] = nullptr;
        }
        vkDevice.destroyImageView(shadowMaskArraySampledView);
        vkDevice.destroyImage(shadowMaskImage);
        device.getMemoryManager().free(shadowMaskAllocation);
        shadowMaskAllocation = {};
    }

    void RTPointShadowPipeline::createSamplers()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;

        depthSampler = vkDevice.createSampler(samplerInfo);
        normalSampler = vkDevice.createSampler(samplerInfo);

        vk::SamplerCreateInfo maskSamplerInfo = samplerInfo;
        maskSamplerInfo.magFilter = vk::Filter::eLinear;
        maskSamplerInfo.minFilter = vk::Filter::eLinear;
        shadowMaskSampler = vkDevice.createSampler(maskSamplerInfo);
    }

    void RTPointShadowPipeline::createParamsBuffer()
    {
        paramsBuffer.create(device.getLogicalDevice(), device.getPhysicalDevice(),
                            sizeof(RTShadowParams), device.getMemoryManager());
    }

    void RTPointShadowPipeline::createDescriptorLayouts(vk::DescriptorSetLayout tlasLayout)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Set 1: input (depth sampler + normal sampler + params UBO) — same layout as rt_shadow.glsl
        std::array<vk::DescriptorSetLayoutBinding, 3> inputBindings{};
        inputBindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        inputBindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        inputBindings[2] = {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute};

        vk::DescriptorSetLayoutCreateInfo inputLayoutInfo{};
        inputLayoutInfo.bindingCount = static_cast<uint32_t>(inputBindings.size());
        inputLayoutInfo.pBindings = inputBindings.data();
        inputLayout = vkDevice.createDescriptorSetLayout(inputLayoutInfo);

        // Set 2: output (single storage image — bound to one layer view per dispatch)
        vk::DescriptorSetLayoutBinding outputBinding{0, vk::DescriptorType::eStorageImage, 1,
                                                      vk::ShaderStageFlagBits::eCompute};
        vk::DescriptorSetLayoutCreateInfo outputLayoutInfo{};
        outputLayoutInfo.bindingCount = 1;
        outputLayoutInfo.pBindings = &outputBinding;
        outputLayout = vkDevice.createDescriptorSetLayout(outputLayoutInfo);

        // Fragment sampler layout (set 16): sampler2DArray
        vk::DescriptorSetLayoutBinding maskBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
                                                    vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo maskLayoutInfo{};
        maskLayoutInfo.bindingCount = 1;
        maskLayoutInfo.pBindings = &maskBinding;
        shadowMaskSamplerLayout = vkDevice.createDescriptorSetLayout(maskLayoutInfo);
    }

    void RTPointShadowPipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 2 * core::MAX_FRAMES_IN_FLIGHT};
        poolSizes[1] = {vk::DescriptorType::eUniformBuffer, core::MAX_FRAMES_IN_FLIGHT};
        poolSizes[2] = {vk::DescriptorType::eStorageImage, MAX_SLICES};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = MAX_SLICES + core::MAX_FRAMES_IN_FLIGHT;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = vkDevice.createDescriptorPool(poolInfo);

        vk::DescriptorPoolSize maskPoolSize{vk::DescriptorType::eCombinedImageSampler, 1};
        vk::DescriptorPoolCreateInfo maskPoolInfo{};
        maskPoolInfo.maxSets = 1;
        maskPoolInfo.poolSizeCount = 1;
        maskPoolInfo.pPoolSizes = &maskPoolSize;
        shadowMaskSamplerPool = vkDevice.createDescriptorPool(maskPoolInfo);
    }

    void RTPointShadowPipeline::allocateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &inputLayout;
            inputDescSet[i] = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }

        for (uint32_t k = 0; k < MAX_SLICES; ++k)
        {
            vk::DescriptorSetAllocateInfo outputAllocInfo{};
            outputAllocInfo.descriptorPool = descriptorPool;
            outputAllocInfo.descriptorSetCount = 1;
            outputAllocInfo.pSetLayouts = &outputLayout;
            outputDescSet[k] = vkDevice.allocateDescriptorSets(outputAllocInfo)[0];
        }

        vk::DescriptorSetAllocateInfo maskAllocInfo{};
        maskAllocInfo.descriptorPool = shadowMaskSamplerPool;
        maskAllocInfo.descriptorSetCount = 1;
        maskAllocInfo.pSetLayouts = &shadowMaskSamplerLayout;
        shadowMaskSamplerDescSet = vkDevice.allocateDescriptorSets(maskAllocInfo)[0];
    }

    void RTPointShadowPipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/shadow/rt_shadow_point.glsl");

        if (shader->getShaderStages().empty())
        {
            vfLogError("RTPointShadowPipeline: Failed to compile shader: {}", shader->getLastCompilationError());
            return;
        }

        std::array<vk::DescriptorSetLayout, 3> setLayouts = {cachedTlasLayout, inputLayout, outputLayout};

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(RTPointShadowPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = shader->getShaderStages()[0];
        pipelineInfo.layout = pipelineLayout;
        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
    }

    void RTPointShadowPipeline::updateOutputDescriptors()
    {
        for (uint32_t k = 0; k < MAX_SLICES; ++k)
        {
            vk::DescriptorImageInfo maskInfo{};
            maskInfo.imageView = shadowMaskLayerStorageViews[k];
            maskInfo.imageLayout = vk::ImageLayout::eGeneral;

            vk::WriteDescriptorSet write{};
            write.dstSet = outputDescSet[k];
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eStorageImage;
            write.pImageInfo = &maskInfo;

            device.getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
        }
    }

    void RTPointShadowPipeline::createShadowMaskSamplerDescriptor()
    {
        vk::DescriptorImageInfo maskInfo{};
        maskInfo.sampler = shadowMaskSampler;
        maskInfo.imageView = shadowMaskArraySampledView;
        maskInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = shadowMaskSamplerDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &maskInfo;

        device.getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
    }

    void RTPointShadowPipeline::dispatch(vk::CommandBuffer cmd,
                                          vk::ImageView depthView,
                                          vk::Image depthImage,
                                          vk::ImageView normalView,
                                          vk::Image normalImage,
                                          vk::DescriptorSet tlasDescriptorSet,
                                          const glm::mat4& invViewProjection,
                                          const glm::vec3& cameraPos,
                                          float farPlane,
                                          uint32_t screenWidth, uint32_t screenHeight,
                                          const std::vector<RTPointDispatchInfo>& lights,
                                          bool skipFinalTransitions,
                                          uint32_t frameIndex)
    {
        if (!initialized || !computePipeline || lights.empty()) return;

        uint32_t fi = frameIndex % core::MAX_FRAMES_IN_FLIGHT;
        paramsBuffer.setFrame(fi);

        RTShadowParams params{};
        params.invViewProjection = invViewProjection;
        params.screenParams = glm::vec4(
            static_cast<float>(screenWidth), static_cast<float>(screenHeight),
            1.0f / static_cast<float>(screenWidth), 1.0f / static_cast<float>(screenHeight));
        params.cameraPosition = glm::vec4(cameraPos, farPlane);
        paramsBuffer.write(&params, sizeof(RTShadowParams));

        // Input descriptor set (depth/normal/params) — shared by every light this frame.
        vk::DescriptorImageInfo depthInfo{};
        depthInfo.sampler = depthSampler;
        depthInfo.imageView = depthView;
        depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorImageInfo normalInfo{};
        normalInfo.sampler = normalSampler;
        normalInfo.imageView = normalView;
        normalInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorBufferInfo paramsInfo{};
        paramsInfo.buffer = paramsBuffer.getBuffer();
        paramsInfo.offset = 0;
        paramsInfo.range = sizeof(RTShadowParams);

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0].dstSet = inputDescSet[fi];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo = &depthInfo;
        writes[1].dstSet = inputDescSet[fi];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].pImageInfo = &normalInfo;
        writes[2].dstSet = inputDescSet[fi];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[2].pBufferInfo = &paramsInfo;
        device.getLogicalDevice().updateDescriptorSets(
            static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // Transition depth/normal to shader read (once for all lights).
        core::ImageUtilities::transitionImageLayout(cmd, depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eDepth);
        core::ImageUtilities::transitionImageLayout(cmd, normalImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Transition the whole mask array (all layers) to general for compute write.
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrier.newLayout = vk::ImageLayout::eGeneral;
            barrier.image = shadowMaskImage;
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, MAX_SLICES};

            if (firstFrame) {
                barrier.srcAccessMask = {};
                barrier.oldLayout = vk::ImageLayout::eUndefined;
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            } else if (shadowMaskInGeneral) {
                barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = vk::ImageLayout::eGeneral;
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            } else {
                barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            }
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        uint32_t groupsX = (screenWidth + 7) / 8;
        uint32_t groupsY = (screenHeight + 7) / 8;

        for (const RTPointDispatchInfo& light : lights)
        {
            uint32_t slice = light.slice;
            if (slice >= MAX_SLICES) continue;

            std::array<vk::DescriptorSet, 3> descSets = {tlasDescriptorSet, inputDescSet[fi], outputDescSet[slice]};
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                                   0, static_cast<uint32_t>(descSets.size()), descSets.data(), 0, nullptr);

            RTPointShadowPushConstants pc{};
            pc.lightPosition = glm::vec4(light.position, light.radius);
            pc.biasParams = glm::vec4(this->normalBias, this->rayTMin, 0.0f, 0.0f);
            cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                              sizeof(RTPointShadowPushConstants), &pc);

            cmd.dispatch(groupsX, groupsY, 1);
        }

        if (!skipFinalTransitions)
        {
            core::ImageUtilities::transitionImageLayout(cmd, shadowMaskImage,
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

            shadowMaskInGeneral = false;
        }
        else
        {
            shadowMaskInGeneral = true;
        }

        firstFrame = false;
    }
}
