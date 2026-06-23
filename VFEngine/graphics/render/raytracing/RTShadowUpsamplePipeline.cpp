#include "RTShadowUpsamplePipeline.hpp"
#include "RTShadowSamplers.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "print/Log.hpp"
#include <glm/glm.hpp>
#include <array>

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::raytracing
{
    RTShadowUpsamplePipeline::RTShadowUpsamplePipeline(core::Device& device)
        : device(device), upsampleCore(device)
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
            if (!upsampleCore.init(core::MAX_FRAMES_IN_FLIGHT))
            {
                // UpsampleCore tore down its own objects on failure; nothing of ours is built yet.
                return;
            }
            createDescriptorLayouts();
            createDescriptorPools();
            allocateDescriptorSets();
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

        vkDevice.destroyDescriptorPool(outputSamplerPool);
        vkDevice.destroyDescriptorSetLayout(outputSamplerLayout);

        destroyOutputImage();

        upsampleCore.cleanup();
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

    void RTShadowUpsamplePipeline::createDescriptorLayouts()
    {
        // Output sampler layout — canonical denoised-mask layout (matches RTShadowDenoiser exactly).
        outputSamplerLayout = createDenoisedMaskLayout(device.getLogicalDevice());
    }

    void RTShadowUpsamplePipeline::createDescriptorPools()
    {
        vk::Device vkDevice = device.getLogicalDevice();

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

        vk::DescriptorSetLayout computeLayout = upsampleCore.computeDescriptorLayout();
        for (uint32_t f = 0; f < core::MAX_FRAMES_IN_FLIGHT; ++f)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = upsampleCore.computeDescriptorPool();
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

    void RTShadowUpsamplePipeline::createOutputSamplerDescriptor()
    {
        vk::DescriptorImageInfo maskInfo{};
        maskInfo.sampler = upsampleCore.output();
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
        if (!initialized || !upsampleCore.valid()) return;

        uint32_t fi = frameIndex % core::MAX_FRAMES_IN_FLIGHT;

        vk::Sampler guideSampler = upsampleCore.guide();

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

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, upsampleCore.pipeline());
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, upsampleCore.layout(),
                               0, 1, &computeDescSet[fi], 0, nullptr);

        UpsamplePushConstants pc{};
        pc.dstExtent = glm::uvec2(fullW, fullH);
        pc.invHalfDims = glm::vec2(1.0f / static_cast<float>(halfW), 1.0f / static_cast<float>(halfH));
        pc.invFullDims = glm::vec2(1.0f / static_cast<float>(fullW), 1.0f / static_cast<float>(fullH));
        pc.depthThreshold = depthThreshold;
        pc.normalExp = normalExp;
        pc.reserved0 = 0;
        pc.reserved1 = 0;
        cmd.pushConstants(upsampleCore.layout(), vk::ShaderStageFlagBits::eCompute,
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
