#include "RTLayeredShadowUpsamplePipeline.hpp"
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
        // Mirrors the push_constant block in rt_shadow_upsample.glsl (the shared 2D shader).
        struct UpsamplePushConstants
        {
            glm::uvec2 dstExtent;
            glm::vec2 invHalfDims;
            glm::vec2 invFullDims;
            float depthThreshold;
            float normalExp;
            int32_t reserved0;      // padding; shared 40-byte layout with the directional upsampler
            int32_t reserved1;
        };
        static_assert(sizeof(UpsamplePushConstants) == 40);
    }

    RTLayeredShadowUpsamplePipeline::RTLayeredShadowUpsamplePipeline(core::Device& device)
        : device(device)
    {
    }

    RTLayeredShadowUpsamplePipeline::~RTLayeredShadowUpsamplePipeline()
    {
        cleanup();
    }

    vk::ImageView RTLayeredShadowUpsamplePipeline::makeLayerView(vk::Image image, vk::Format format, uint32_t layer)
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

    void RTLayeredShadowUpsamplePipeline::resize(uint32_t fullW, uint32_t fullH)
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
            vfLogInfo("RTLayeredShadowUpsamplePipeline: Initialized {}x{} x{} slices", fullW, fullH, MAX_SLICES);
            return;
        }

        if (fullW == outputWidth && fullH == outputHeight) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();
        destroyOutputImage();
        createOutputImage(fullW, fullH);
        createOutputSamplerDescriptor();
        firstDispatch = true;
        vfLogInfo("RTLayeredShadowUpsamplePipeline: Resized to {}x{}", fullW, fullH);
    }

    void RTLayeredShadowUpsamplePipeline::cleanup()
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

    void RTLayeredShadowUpsamplePipeline::createOutputImage(uint32_t w, uint32_t h)
    {
        outputWidth = w;
        outputHeight = h;

        core::ImageInfoRequest request(
            device.getLogicalDevice(), device.getPhysicalDevice(),
            w, h, MAX_SLICES, 1,
            vk::Format::eR16Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(request, outputImage, outputAllocation, device.getMemoryManager());

        for (uint32_t k = 0; k < MAX_SLICES; ++k)
            outputLayerStorageView[k] = makeLayerView(outputImage, vk::Format::eR16Sfloat, k);

        core::ImageViewInfoRequest arrayViewReq(
            device.getLogicalDevice(), outputImage,
            vk::Format::eR16Sfloat, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2DArray, MAX_SLICES, 1
        );
        core::ImageUtilities::createImageView(arrayViewReq, outputArraySampledView);
    }

    void RTLayeredShadowUpsamplePipeline::destroyOutputImage()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        for (uint32_t k = 0; k < MAX_SLICES; ++k)
            vkDevice.destroyImageView(outputLayerStorageView[k]);
        vkDevice.destroyImageView(outputArraySampledView);
        vkDevice.destroyImage(outputImage);
        device.getMemoryManager().free(outputAllocation);
        outputAllocation = {};
    }

    void RTLayeredShadowUpsamplePipeline::createSamplers()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::SamplerCreateInfo nearestInfo{};
        nearestInfo.magFilter = vk::Filter::eNearest;
        nearestInfo.minFilter = vk::Filter::eNearest;
        nearestInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        nearestInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        nearestInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        guideSampler = vkDevice.createSampler(nearestInfo);

        vk::SamplerCreateInfo linearInfo = nearestInfo;
        linearInfo.magFilter = vk::Filter::eLinear;
        linearInfo.minFilter = vk::Filter::eLinear;
        outputSampler = vkDevice.createSampler(linearInfo);
    }

    void RTLayeredShadowUpsamplePipeline::createDescriptorLayouts()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Compute set reuses the non-layered shader per slice: half mask (e2D layer sampler) + depth
        // + normal + output (e2D layer storage).
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[2] = {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[3] = {3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        computeLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Output sampler layout — MUST match RTLayeredShadowDenoiser::denoisedMaskSamplerLayout:
        // binding 0, combined image sampler (e2DArray view), fragment stage.
        vk::DescriptorSetLayoutBinding maskBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
                                                   vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo maskLayoutInfo{};
        maskLayoutInfo.bindingCount = 1;
        maskLayoutInfo.pBindings = &maskBinding;
        outputSamplerLayout = vkDevice.createDescriptorSetLayout(maskLayoutInfo);
    }

    void RTLayeredShadowUpsamplePipeline::createDescriptorPools()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        const uint32_t computeSets = core::MAX_FRAMES_IN_FLIGHT * MAX_SLICES;
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 3 * computeSets};
        poolSizes[1] = {vk::DescriptorType::eStorageImage, 1 * computeSets};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = computeSets;
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

    void RTLayeredShadowUpsamplePipeline::allocateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        for (uint32_t f = 0; f < core::MAX_FRAMES_IN_FLIGHT; ++f)
        {
            std::array<vk::DescriptorSetLayout, MAX_SLICES> layouts;
            layouts.fill(computeLayout);
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = computePool;
            allocInfo.descriptorSetCount = MAX_SLICES;
            allocInfo.pSetLayouts = layouts.data();
            auto sets = vkDevice.allocateDescriptorSets(allocInfo);
            for (uint32_t s = 0; s < MAX_SLICES; ++s)
                computeDescSet[f][s] = sets[s];
        }

        vk::DescriptorSetAllocateInfo maskAllocInfo{};
        maskAllocInfo.descriptorPool = outputSamplerPool;
        maskAllocInfo.descriptorSetCount = 1;
        maskAllocInfo.pSetLayouts = &outputSamplerLayout;
        outputSamplerDescSet = vkDevice.allocateDescriptorSets(maskAllocInfo)[0];
    }

    void RTLayeredShadowUpsamplePipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/shadow/rt_shadow_upsample.glsl");
        if (shader->getShaderStages().empty())
        {
            vfLogError("RTLayeredShadowUpsamplePipeline: Failed to compile shader: {}", shader->getLastCompilationError());
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

    void RTLayeredShadowUpsamplePipeline::createOutputSamplerDescriptor()
    {
        vk::DescriptorImageInfo maskInfo{};
        maskInfo.sampler = outputSampler;
        maskInfo.imageView = outputArraySampledView;
        maskInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = outputSamplerDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &maskInfo;

        device.getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
    }

    void RTLayeredShadowUpsamplePipeline::dispatch(vk::CommandBuffer cmd,
                                                   const std::vector<RTLayeredUpsampleInfo>& slices,
                                                   vk::ImageView fullDepthView,
                                                   vk::ImageView fullNormalView,
                                                   uint32_t halfW, uint32_t halfH,
                                                   uint32_t fullW, uint32_t fullH,
                                                   float depthThreshold,
                                                   float normalExp,
                                                   uint32_t frameIndex)
    {
        if (!initialized || !computePipeline || slices.empty()) return;

        uint32_t fi = frameIndex % core::MAX_FRAMES_IN_FLIGHT;

        // Transition the WHOLE output array to eGeneral once (all layers) for the compute writes.
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrier.newLayout = vk::ImageLayout::eGeneral;
            barrier.image = outputImage;
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, MAX_SLICES};
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
                barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            }
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        UpsamplePushConstants pc{};
        pc.dstExtent = glm::uvec2(fullW, fullH);
        pc.invHalfDims = glm::vec2(1.0f / static_cast<float>(halfW), 1.0f / static_cast<float>(halfH));
        pc.invFullDims = glm::vec2(1.0f / static_cast<float>(fullW), 1.0f / static_cast<float>(fullH));
        pc.depthThreshold = depthThreshold;
        pc.normalExp = normalExp;
        // Addressing is per-bound e2D view (one slice per dispatch), so no slice index is needed.
        pc.reserved0 = 0;
        pc.reserved1 = 0;

        const uint32_t groupsX = (fullW + 7) / 8;
        const uint32_t groupsY = (fullH + 7) / 8;

        for (const auto& s : slices)
        {
            if (s.slice >= MAX_SLICES) continue;

            vk::DescriptorSet set = computeDescSet[fi][s.slice];

            vk::DescriptorImageInfo halfInfo{};
            halfInfo.sampler = guideSampler;
            halfInfo.imageView = s.halfResDenoisedLayerView;
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
            outInfo.imageView = outputLayerStorageView[s.slice];
            outInfo.imageLayout = vk::ImageLayout::eGeneral;

            std::array<vk::WriteDescriptorSet, 4> writes{};
            writes[0].dstSet = set;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].pImageInfo = &halfInfo;

            writes[1].dstSet = set;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[1].pImageInfo = &depthInfo;

            writes[2].dstSet = set;
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[2].pImageInfo = &normalInfo;

            writes[3].dstSet = set;
            writes[3].dstBinding = 3;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = vk::DescriptorType::eStorageImage;
            writes[3].pImageInfo = &outInfo;

            device.getLogicalDevice().updateDescriptorSets(
                static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                                   0, 1, &set, 0, nullptr);
            cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                              0, sizeof(UpsamplePushConstants), &pc);
            cmd.dispatch(groupsX, groupsY, 1);
        }

        // Output array: general -> shader read for the set-15/16 sampler this frame (all layers).
        core::ImageUtilities::transitionImageLayout(cmd, outputImage,
            vk::ImageLayout::eGeneral,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, MAX_SLICES);

        firstDispatch = false;
    }
}
