#include "RTShadowPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
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
    RTShadowPipeline::RTShadowPipeline(core::Device& device)
        : device(device)
    {
    }

    RTShadowPipeline::~RTShadowPipeline()
    {
        cleanup();
    }

    void RTShadowPipeline::init(uint32_t width, uint32_t height,
                                 vk::DescriptorSetLayout tlasLayout)
    {
        if (initialized) return;
        if (!device.isRayQuerySupported())
        {
            vfLogWarning("RTShadowPipeline: Ray query not supported");
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
        updateOutputDescriptor();
        createShadowMaskSamplerDescriptor();

        initialized = true;
        firstFrame = true;
        vfLogInfo("RTShadowPipeline: Initialized {}x{}", width, height);
    }

    void RTShadowPipeline::cleanup()
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

        vkDevice.destroyImageView(shadowMaskStorageView);
        vkDevice.destroyImageView(shadowMaskSampledView);
        vkDevice.destroyImage(shadowMaskImage);
        device.getMemoryManager().free(shadowMaskAllocation);
        shadowMaskAllocation = {};

        shader.reset();
        initialized = false;
    }

    void RTShadowPipeline::resize(uint32_t width, uint32_t height)
    {
        if (!initialized) return;
        if (width == maskWidth && height == maskHeight) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        vkDevice.destroyImageView(shadowMaskStorageView);
        vkDevice.destroyImageView(shadowMaskSampledView);
        vkDevice.destroyImage(shadowMaskImage);
        device.getMemoryManager().free(shadowMaskAllocation);
        shadowMaskAllocation = {};

        createShadowMaskImage(width, height);
        updateOutputDescriptor();
        createShadowMaskSamplerDescriptor();
        firstFrame = true;

        vfLogInfo("RTShadowPipeline: Resized to {}x{}", width, height);
    }

    void RTShadowPipeline::createShadowMaskImage(uint32_t w, uint32_t h)
    {
        maskWidth = w;
        maskHeight = h;

        core::ImageInfoRequest request(
            device.getLogicalDevice(), device.getPhysicalDevice(),
            w, h, 1, 1,
            vk::Format::eR8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(request, shadowMaskImage, shadowMaskAllocation, device.getMemoryManager());

        // Storage view (for compute write)
        core::ImageViewInfoRequest storageViewReq(
            device.getLogicalDevice(), shadowMaskImage,
            vk::Format::eR8Unorm, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(storageViewReq, shadowMaskStorageView);

        // Sampled view (for fragment read) - same format, separate view
        core::ImageViewInfoRequest sampledViewReq(
            device.getLogicalDevice(), shadowMaskImage,
            vk::Format::eR8Unorm, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(sampledViewReq, shadowMaskSampledView);
    }

    void RTShadowPipeline::createSamplers()
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

        // Shadow mask sampler (linear for smooth sampling in fragment shader)
        vk::SamplerCreateInfo maskSamplerInfo = samplerInfo;
        maskSamplerInfo.magFilter = vk::Filter::eLinear;
        maskSamplerInfo.minFilter = vk::Filter::eLinear;
        shadowMaskSampler = vkDevice.createSampler(maskSamplerInfo);
    }

    void RTShadowPipeline::createParamsBuffer()
    {
        paramsBuffer.create(device.getLogicalDevice(), device.getPhysicalDevice(),
                            sizeof(RTShadowParams), device.getMemoryManager());
    }

    void RTShadowPipeline::createDescriptorLayouts(vk::DescriptorSetLayout tlasLayout)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Set 1: Input data (depth sampler + normal sampler + params UBO)
        std::array<vk::DescriptorSetLayoutBinding, 3> inputBindings{};
        inputBindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        inputBindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        inputBindings[2] = {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute};

        vk::DescriptorSetLayoutCreateInfo inputLayoutInfo{};
        inputLayoutInfo.bindingCount = static_cast<uint32_t>(inputBindings.size());
        inputLayoutInfo.pBindings = inputBindings.data();
        inputLayout = vkDevice.createDescriptorSetLayout(inputLayoutInfo);

        // Set 2: Output (storage image)
        vk::DescriptorSetLayoutBinding outputBinding{0, vk::DescriptorType::eStorageImage, 1,
                                                      vk::ShaderStageFlagBits::eCompute};
        vk::DescriptorSetLayoutCreateInfo outputLayoutInfo{};
        outputLayoutInfo.bindingCount = 1;
        outputLayoutInfo.pBindings = &outputBinding;
        outputLayout = vkDevice.createDescriptorSetLayout(outputLayoutInfo);

        // Shadow mask sampler layout (for fragment shader)
        vk::DescriptorSetLayoutBinding maskBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
                                                    vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo maskLayoutInfo{};
        maskLayoutInfo.bindingCount = 1;
        maskLayoutInfo.pBindings = &maskBinding;
        shadowMaskSamplerLayout = vkDevice.createDescriptorSetLayout(maskLayoutInfo);
    }

    void RTShadowPipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 2 * core::MAX_FRAMES_IN_FLIGHT}; // depth + normal per frame
        poolSizes[1] = {vk::DescriptorType::eUniformBuffer, core::MAX_FRAMES_IN_FLIGHT};             // params per frame
        poolSizes[2] = {vk::DescriptorType::eStorageImage, 1};                                       // shadow mask output

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1 + core::MAX_FRAMES_IN_FLIGHT; // 1 output + N input
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = vkDevice.createDescriptorPool(poolInfo);

        // Separate pool for fragment shader sampler
        vk::DescriptorPoolSize maskPoolSize{vk::DescriptorType::eCombinedImageSampler, 1};
        vk::DescriptorPoolCreateInfo maskPoolInfo{};
        maskPoolInfo.maxSets = 1;
        maskPoolInfo.poolSizeCount = 1;
        maskPoolInfo.pPoolSizes = &maskPoolSize;
        shadowMaskSamplerPool = vkDevice.createDescriptorPool(maskPoolInfo);
    }

    void RTShadowPipeline::allocateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Allocate per-frame input descriptor sets
        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &inputLayout;
            inputDescSet[i] = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }

        // Allocate single output descriptor set
        vk::DescriptorSetAllocateInfo outputAllocInfo{};
        outputAllocInfo.descriptorPool = descriptorPool;
        outputAllocInfo.descriptorSetCount = 1;
        outputAllocInfo.pSetLayouts = &outputLayout;
        outputDescSet = vkDevice.allocateDescriptorSets(outputAllocInfo)[0];

        // Fragment shader sampler set
        vk::DescriptorSetAllocateInfo maskAllocInfo{};
        maskAllocInfo.descriptorPool = shadowMaskSamplerPool;
        maskAllocInfo.descriptorSetCount = 1;
        maskAllocInfo.pSetLayouts = &shadowMaskSamplerLayout;
        shadowMaskSamplerDescSet = vkDevice.allocateDescriptorSets(maskAllocInfo)[0];
    }

    void RTShadowPipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/shadow/rt_shadow.glsl");

        if (shader->getShaderStages().empty())
        {
            vfLogError("RTShadowPipeline: Failed to compile shader: {}", shader->getLastCompilationError());
            return;
        }

        // Pipeline layout: set 0 = TLAS, set 1 = input, set 2 = output
        std::array<vk::DescriptorSetLayout, 3> setLayouts = {cachedTlasLayout, inputLayout, outputLayout};

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(RTShadowPushConstants);

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

    void RTShadowPipeline::updateOutputDescriptor()
    {
        vk::DescriptorImageInfo maskInfo{};
        maskInfo.imageView = shadowMaskStorageView;
        maskInfo.imageLayout = vk::ImageLayout::eGeneral;

        vk::WriteDescriptorSet write{};
        write.dstSet = outputDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageImage;
        write.pImageInfo = &maskInfo;

        device.getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
    }

    void RTShadowPipeline::createShadowMaskSamplerDescriptor()
    {
        vk::DescriptorImageInfo maskInfo{};
        maskInfo.sampler = shadowMaskSampler;
        maskInfo.imageView = shadowMaskSampledView;
        maskInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = shadowMaskSamplerDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &maskInfo;

        device.getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
    }

    void RTShadowPipeline::dispatch(vk::CommandBuffer cmd,
                                     vk::ImageView depthView,
                                     vk::Image depthImage,
                                     vk::ImageView normalView,
                                     vk::Image normalImage,
                                     vk::DescriptorSet tlasDescriptorSet,
                                     const glm::mat4& invViewProjection,
                                     const glm::vec3& cameraPos,
                                     float farPlane,
                                     const glm::vec3& lightDirection,
                                     uint32_t screenWidth, uint32_t screenHeight,
                                     bool skipFinalTransitions,
                                     uint32_t frameIndex)
    {
        if (!initialized || !computePipeline) return;

        // Use caller-provided frame index aligned with OffScreenViewPort fence scheme
        uint32_t fi = frameIndex % core::MAX_FRAMES_IN_FLIGHT;
        paramsBuffer.setFrame(fi);

        // Update params UBO for this frame slot
        RTShadowParams params{};
        params.invViewProjection = invViewProjection;
        params.screenParams = glm::vec4(
            static_cast<float>(screenWidth), static_cast<float>(screenHeight),
            1.0f / static_cast<float>(screenWidth), 1.0f / static_cast<float>(screenHeight));
        params.cameraPosition = glm::vec4(cameraPos, farPlane);
        paramsBuffer.write(&params, sizeof(RTShadowParams));

        // Update this frame's input descriptor set
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

        // Transition depth: attachment -> shader read
        core::ImageUtilities::transitionImageLayout(cmd, depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eDepth);

        // Transition normal: color attachment -> shader read
        core::ImageUtilities::transitionImageLayout(cmd, normalImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Transition shadow mask to general for compute write
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrier.newLayout = vk::ImageLayout::eGeneral;
            barrier.image = shadowMaskImage;
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            if (firstFrame) {
                barrier.srcAccessMask = {};
                barrier.oldLayout = vk::ImageLayout::eUndefined;
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            } else if (shadowMaskInGeneral) {
                // Denoiser left it in eGeneral last frame — just need execution+memory barrier
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

        // Bind and dispatch
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        std::array<vk::DescriptorSet, 3> descSets = {tlasDescriptorSet, inputDescSet[fi], outputDescSet};
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, static_cast<uint32_t>(descSets.size()), descSets.data(), 0, nullptr);

        RTShadowPushConstants pc{};
        pc.lightDirection = glm::vec4(-glm::normalize(lightDirection), this->maxRayDistance);
        pc.biasParams = glm::vec4(this->normalBias, this->rayTMin, 0.0f, 0.0f);
        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(RTShadowPushConstants), &pc);

        uint32_t groupsX = (screenWidth + 7) / 8;
        uint32_t groupsY = (screenHeight + 7) / 8;
        cmd.dispatch(groupsX, groupsY, 1);

        if (!skipFinalTransitions)
        {
            // Transition shadow mask: general -> shader read (for fragment shader)
            core::ImageUtilities::transitionImageLayout(cmd, shadowMaskImage,
                vk::ImageLayout::eGeneral,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            // Transition depth back: shader read -> attachment
            core::ImageUtilities::transitionImageLayout(cmd, depthImage,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eDepthStencilAttachmentOptimal,
                vk::ImageAspectFlagBits::eDepth);

            // Transition normal back: shader read -> color attachment
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
