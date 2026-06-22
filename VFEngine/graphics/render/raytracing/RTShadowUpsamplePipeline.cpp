#include "RTShadowUpsamplePipeline.hpp"
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
    namespace
    {
        // Mirrors the push_constant block in rt_shadow_upsample.glsl (std430-compatible layout).
        struct UpsamplePushConstants
        {
            glm::uvec2 dstExtent;   // offset 0
            glm::vec2 invHalfDims;  // offset 8
            glm::vec2 invFullDims;  // offset 16
            float depthThreshold;   // offset 24
            float normalExp;        // offset 28
            int32_t reserved0;      // offset 32 (padding; shared 40-byte layout with the layered upsampler)
            int32_t reserved1;      // offset 36
        };
        static_assert(sizeof(UpsamplePushConstants) == 40);
    }

    RTShadowUpsamplePipeline::RTShadowUpsamplePipeline(core::Device& device)
        : device(device)
    {
    }

    RTShadowUpsamplePipeline::~RTShadowUpsamplePipeline()
    {
        cleanup();
    }

    void RTShadowUpsamplePipeline::resize(uint32_t fullW, uint32_t fullH)
    {
        if (fullW == 0 || fullH == 0) return;

        if (!initialized)
        {
            createSamplers();
            createDescriptorLayouts();
            createDescriptorPools();
            allocateDescriptorSets();
            createComputePipeline();
            if (!computePipeline)
            {
                // Pipeline creation failed — tear down what we built and stay uninitialized.
                // pipelineLayout is created inside createComputePipeline() before the pipeline
                // itself, so it must be destroyed here (cleanup() early-returns while !initialized).
                vk::Device vkDevice = device.getLogicalDevice();
                vkDevice.destroyPipelineLayout(pipelineLayout);
                vkDevice.destroyDescriptorPool(computePool);
                vkDevice.destroyDescriptorPool(outputSamplerPool);
                vkDevice.destroyDescriptorSetLayout(computeLayout);
                vkDevice.destroyDescriptorSetLayout(outputSamplerLayout);
                vkDevice.destroySampler(outputSampler);
                vkDevice.destroySampler(guideSampler);
                shader.reset();
                return;
            }
            createOutputImage(fullW, fullH);
            createOutputSamplerDescriptor();
            initialized = true;
            firstDispatch = true;
            vfLogInfo("RTShadowUpsamplePipeline: Initialized {}x{}", fullW, fullH);
            return;
        }

        if (fullW == outputWidth && fullH == outputHeight) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();
        destroyOutputImage();
        createOutputImage(fullW, fullH);
        createOutputSamplerDescriptor();
        firstDispatch = true;
        vfLogInfo("RTShadowUpsamplePipeline: Resized to {}x{}", fullW, fullH);
    }

    void RTShadowUpsamplePipeline::cleanup()
    {
        if (!initialized) return;
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        vkDevice.destroyPipeline(computePipeline);
        vkDevice.destroyPipelineLayout(pipelineLayout);

        vkDevice.destroyDescriptorPool(computePool);
        vkDevice.destroyDescriptorPool(outputSamplerPool);
        vkDevice.destroyDescriptorSetLayout(computeLayout);
        vkDevice.destroyDescriptorSetLayout(outputSamplerLayout);

        vkDevice.destroySampler(outputSampler);
        vkDevice.destroySampler(guideSampler);

        destroyOutputImage();

        shader.reset();
        initialized = false;
    }

    void RTShadowUpsamplePipeline::createOutputImage(uint32_t w, uint32_t h)
    {
        outputWidth = w;
        outputHeight = h;

        core::ImageInfoRequest request(
            device.getLogicalDevice(), device.getPhysicalDevice(),
            w, h, 1, 1,
            vk::Format::eR16Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(request, outputImage, outputAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest storageViewReq(
            device.getLogicalDevice(), outputImage,
            vk::Format::eR16Sfloat, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(storageViewReq, outputStorageView);

        core::ImageViewInfoRequest sampledViewReq(
            device.getLogicalDevice(), outputImage,
            vk::Format::eR16Sfloat, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(sampledViewReq, outputSampledView);
    }

    void RTShadowUpsamplePipeline::destroyOutputImage()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.destroyImageView(outputStorageView);
        vkDevice.destroyImageView(outputSampledView);
        vkDevice.destroyImage(outputImage);
        device.getMemoryManager().free(outputAllocation);
        outputAllocation = {};
    }

    void RTShadowUpsamplePipeline::createSamplers()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Nearest sampler for depth/normal guides AND the half-res mask (the joint-bilateral math
        // does its own weighting; we explicitly fetch the 4 half taps, so no hardware bilinear).
        vk::SamplerCreateInfo nearestInfo{};
        nearestInfo.magFilter = vk::Filter::eNearest;
        nearestInfo.minFilter = vk::Filter::eNearest;
        nearestInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        nearestInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        nearestInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        guideSampler = vkDevice.createSampler(nearestInfo);

        // Linear sampler for the full-res output's fragment-shader reads — matches the denoiser's
        // denoisedMaskSampler so the consumer behaves identically.
        vk::SamplerCreateInfo linearInfo = nearestInfo;
        linearInfo.magFilter = vk::Filter::eLinear;
        linearInfo.minFilter = vk::Filter::eLinear;
        outputSampler = vkDevice.createSampler(linearInfo);
    }

    void RTShadowUpsamplePipeline::createDescriptorLayouts()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Compute set: half mask (sampler) + depth (sampler) + normal (sampler) + output (storage).
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[2] = {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[3] = {3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        computeLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Output sampler layout — MUST match RTShadowDenoiser::denoisedMaskSamplerLayout exactly:
        // binding 0, combined image sampler, fragment stage.
        vk::DescriptorSetLayoutBinding maskBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
                                                   vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo maskLayoutInfo{};
        maskLayoutInfo.bindingCount = 1;
        maskLayoutInfo.pBindings = &maskBinding;
        outputSamplerLayout = vkDevice.createDescriptorSetLayout(maskLayoutInfo);
    }

    void RTShadowUpsamplePipeline::createDescriptorPools()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 3 * core::MAX_FRAMES_IN_FLIGHT};
        poolSizes[1] = {vk::DescriptorType::eStorageImage, 1 * core::MAX_FRAMES_IN_FLIGHT};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = core::MAX_FRAMES_IN_FLIGHT;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        computePool = vkDevice.createDescriptorPool(poolInfo);

        vk::DescriptorPoolSize maskPoolSize{vk::DescriptorType::eCombinedImageSampler, 1};
        vk::DescriptorPoolCreateInfo maskPoolInfo{};
        maskPoolInfo.maxSets = 1;
        maskPoolInfo.poolSizeCount = 1;
        maskPoolInfo.pPoolSizes = &maskPoolSize;
        outputSamplerPool = vkDevice.createDescriptorPool(maskPoolInfo);
    }

    void RTShadowUpsamplePipeline::allocateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        for (uint32_t f = 0; f < core::MAX_FRAMES_IN_FLIGHT; ++f)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = computePool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &computeLayout;
            computeDescSet[f] = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }

        vk::DescriptorSetAllocateInfo maskAllocInfo{};
        maskAllocInfo.descriptorPool = outputSamplerPool;
        maskAllocInfo.descriptorSetCount = 1;
        maskAllocInfo.pSetLayouts = &outputSamplerLayout;
        outputSamplerDescSet = vkDevice.allocateDescriptorSets(maskAllocInfo)[0];
    }

    void RTShadowUpsamplePipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/shadow/rt_shadow_upsample.glsl");
        if (shader->getShaderStages().empty())
        {
            vfLogError("RTShadowUpsamplePipeline: Failed to compile shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(UpsamplePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = shader->getShaderStages()[0];
        pipelineInfo.layout = pipelineLayout;
        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
    }

    void RTShadowUpsamplePipeline::createOutputSamplerDescriptor()
    {
        vk::DescriptorImageInfo maskInfo{};
        maskInfo.sampler = outputSampler;
        maskInfo.imageView = outputSampledView;
        maskInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = outputSamplerDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &maskInfo;

        device.getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
    }

    void RTShadowUpsamplePipeline::dispatch(vk::CommandBuffer cmd,
                                            vk::ImageView halfResDenoisedView,
                                            vk::ImageView fullDepthView,
                                            vk::ImageView fullNormalView,
                                            uint32_t halfW, uint32_t halfH,
                                            uint32_t fullW, uint32_t fullH,
                                            float depthThreshold,
                                            float normalExp,
                                            uint32_t frameIndex)
    {
        if (!initialized || !computePipeline) return;

        uint32_t fi = frameIndex % core::MAX_FRAMES_IN_FLIGHT;

        // Update this frame's compute descriptor set.
        vk::DescriptorImageInfo halfInfo{};
        halfInfo.sampler = guideSampler;
        halfInfo.imageView = halfResDenoisedView;
        halfInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorImageInfo depthInfo{};
        depthInfo.sampler = guideSampler;
        depthInfo.imageView = fullDepthView;
        depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorImageInfo normalInfo{};
        normalInfo.sampler = guideSampler;
        normalInfo.imageView = fullNormalView;
        normalInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorImageInfo outInfo{};
        outInfo.imageView = outputStorageView;
        outInfo.imageLayout = vk::ImageLayout::eGeneral;

        std::array<vk::WriteDescriptorSet, 4> writes{};
        writes[0].dstSet = computeDescSet[fi];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo = &halfInfo;

        writes[1].dstSet = computeDescSet[fi];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].pImageInfo = &depthInfo;

        writes[2].dstSet = computeDescSet[fi];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[2].pImageInfo = &normalInfo;

        writes[3].dstSet = computeDescSet[fi];
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = vk::DescriptorType::eStorageImage;
        writes[3].pImageInfo = &outInfo;

        device.getLogicalDevice().updateDescriptorSets(
            static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // Transition the output to eGeneral for the compute write.
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrier.newLayout = vk::ImageLayout::eGeneral;
            barrier.image = outputImage;
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            if (firstDispatch)
            {
                barrier.srcAccessMask = {};
                barrier.oldLayout = vk::ImageLayout::eUndefined;
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            else
            {
                // Was left in eShaderReadOnlyOptimal at the end of the previous dispatch; the
                // fragment shader (set 13) sampled it last frame.
                barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            }
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, 1, &computeDescSet[fi], 0, nullptr);

        UpsamplePushConstants pc{};
        pc.dstExtent = glm::uvec2(fullW, fullH);
        pc.invHalfDims = glm::vec2(1.0f / static_cast<float>(halfW), 1.0f / static_cast<float>(halfH));
        pc.invFullDims = glm::vec2(1.0f / static_cast<float>(fullW), 1.0f / static_cast<float>(fullH));
        pc.depthThreshold = depthThreshold;
        pc.normalExp = normalExp;
        pc.reserved0 = 0;
        pc.reserved1 = 0;
        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(UpsamplePushConstants), &pc);

        uint32_t groupsX = (fullW + 7) / 8;
        uint32_t groupsY = (fullH + 7) / 8;
        cmd.dispatch(groupsX, groupsY, 1);

        // Output: general -> shader read for the set-13 sampler this frame.
        core::ImageUtilities::transitionImageLayout(cmd, outputImage,
            vk::ImageLayout::eGeneral,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        firstDispatch = false;
    }
}
