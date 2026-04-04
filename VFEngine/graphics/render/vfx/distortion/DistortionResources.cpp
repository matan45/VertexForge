#include "DistortionResources.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/Utilities.hpp"
#include "../../../core/OffScreen.hpp"
#include "print/Log.hpp"

namespace render::vfx
{
    DistortionResources::DistortionResources(core::Device& device)
        : device(device)
    {
    }

    DistortionResources::~DistortionResources()
    {
        cleanup();
    }

    void DistortionResources::init(vk::Format colorFormat, vk::Format depthFormat,
                                    vk::Extent2D swapExtent, vk::ImageView sceneDepthView,
                                    const core::OffscreenResources& offscreen)
    {
        extent = swapExtent;
        depthView = sceneDepthView;

        createDistortionImage(swapExtent.width, swapExtent.height);
        createSceneColorCopyImage(colorFormat, swapExtent.width, swapExtent.height);
        createSampler();
        createCompositeDescriptorLayout();
        createCompositeDescriptorPool();
        allocateCompositeDescriptorSet();
        updateCompositeDescriptorSet();
        transitionImagesInitial();
        initialized = true;
    }

    void DistortionResources::recreate(vk::Format colorFormat, vk::Format depthFormat,
                                        vk::Extent2D swapExtent, vk::ImageView sceneDepthView,
                                        const core::OffscreenResources& offscreen)
    {
        cleanup();
        init(colorFormat, depthFormat, swapExtent, sceneDepthView, offscreen);
    }

    void DistortionResources::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (compositeDescriptorPool) { vkDevice.destroyDescriptorPool(compositeDescriptorPool); compositeDescriptorPool = nullptr; }
        if (compositeDescriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(compositeDescriptorSetLayout); compositeDescriptorSetLayout = nullptr; }

        if (linearSampler) { vkDevice.destroySampler(linearSampler); linearSampler = nullptr; }

        if (distortionView) { vkDevice.destroyImageView(distortionView); distortionView = nullptr; }
        if (distortionImage) { vkDevice.destroyImage(distortionImage); distortionImage = nullptr; }
        if (distortionAllocation) { device.getMemoryManager().free(distortionAllocation); distortionAllocation = {}; }

        if (sceneColorCopyView) { vkDevice.destroyImageView(sceneColorCopyView); sceneColorCopyView = nullptr; }
        if (sceneColorCopyImage) { vkDevice.destroyImage(sceneColorCopyImage); sceneColorCopyImage = nullptr; }
        if (sceneColorCopyAllocation) { device.getMemoryManager().free(sceneColorCopyAllocation); sceneColorCopyAllocation = {}; }

        depthView = nullptr;
        initialized = false;
    }

    void DistortionResources::createDistortionImage(uint32_t width, uint32_t height)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
            width, height, 1, 1,
            vk::Format::eR16G16Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::ImageUtilities::createImage(req, distortionImage, distortionAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest viewReq(vkDevice, distortionImage,
            vk::Format::eR16G16Sfloat,
            vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
        core::ImageUtilities::createImageView(viewReq, distortionView);
    }

    void DistortionResources::createSceneColorCopyImage(vk::Format format, uint32_t width, uint32_t height)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
            width, height, 1, 1,
            format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::ImageUtilities::createImage(req, sceneColorCopyImage, sceneColorCopyAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest viewReq(vkDevice, sceneColorCopyImage,
            format,
            vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
        core::ImageUtilities::createImageView(viewReq, sceneColorCopyView);
    }

    void DistortionResources::createSampler()
    {
        vk::SamplerCreateInfo info{};
        info.magFilter = vk::Filter::eLinear;
        info.minFilter = vk::Filter::eLinear;
        info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        info.anisotropyEnable = VK_FALSE;
        info.maxAnisotropy = 1.0f;
        info.mipmapMode = vk::SamplerMipmapMode::eLinear;

        linearSampler = device.getLogicalDevice().createSampler(info);
    }

    void DistortionResources::createCompositeDescriptorLayout()
    {
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

        // Binding 0: Scene color copy
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // Binding 1: Distortion buffer
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        compositeDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void DistortionResources::createCompositeDescriptorPool()
    {
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        compositeDescriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void DistortionResources::allocateCompositeDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = compositeDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &compositeDescriptorSetLayout;

        compositeDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void DistortionResources::updateCompositeDescriptorSet()
    {
        std::array<vk::DescriptorImageInfo, 2> imageInfos{};

        imageInfos[0].sampler = linearSampler;
        imageInfos[0].imageView = sceneColorCopyView;
        imageInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        imageInfos[1].sampler = linearSampler;
        imageInfos[1].imageView = distortionView;
        imageInfos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        std::array<vk::WriteDescriptorSet, 2> writes{};

        writes[0].dstSet = compositeDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo = &imageInfos[0];

        writes[1].dstSet = compositeDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].pImageInfo = &imageInfos[1];

        device.getLogicalDevice().updateDescriptorSets(writes, nullptr);
    }

    void DistortionResources::transitionImagesInitial()
    {
        auto cmd = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), device.getStagingCommandPool());

        core::ImageUtilities::transitionImageLayout(
            cmd.get(), distortionImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::ImageUtilities::transitionImageLayout(
            cmd.get(), sceneColorCopyImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(device, cmd);
    }

    void DistortionResources::copySceneColor(vk::CommandBuffer cmd, vk::Image srcColorImage, uint32_t width, uint32_t height)
    {
        // 1. Transition scene color copy: ShaderReadOnly -> TransferDst
        vk::ImageMemoryBarrier toTransferDst{};
        toTransferDst.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toTransferDst.newLayout = vk::ImageLayout::eTransferDstOptimal;
        toTransferDst.image = sceneColorCopyImage;
        toTransferDst.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        toTransferDst.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        toTransferDst.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                            vk::PipelineStageFlagBits::eTransfer,
                            {}, {}, {}, toTransferDst);

        // 2. Transition source: ShaderReadOnly -> TransferSrc
        vk::ImageMemoryBarrier srcToTransfer{};
        srcToTransfer.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        srcToTransfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        srcToTransfer.image = srcColorImage;
        srcToTransfer.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        srcToTransfer.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        srcToTransfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits::eTransfer,
                            {}, {}, {}, srcToTransfer);

        // 3. Copy
        vk::ImageCopy copyRegion{};
        copyRegion.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        copyRegion.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        copyRegion.extent = vk::Extent3D{width, height, 1};
        cmd.copyImage(srcColorImage, vk::ImageLayout::eTransferSrcOptimal,
                      sceneColorCopyImage, vk::ImageLayout::eTransferDstOptimal,
                      copyRegion);

        // 4. Transition scene color copy: TransferDst -> ShaderReadOnly
        vk::ImageMemoryBarrier toShaderRead{};
        toShaderRead.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        toShaderRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toShaderRead.image = sceneColorCopyImage;
        toShaderRead.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        toShaderRead.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        toShaderRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eFragmentShader,
                            {}, {}, {}, toShaderRead);

        // 5. Transition source: TransferSrc -> ShaderReadOnly (for subsequent passes)
        vk::ImageMemoryBarrier srcBack{};
        srcBack.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        srcBack.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        srcBack.image = srcColorImage;
        srcBack.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        srcBack.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        srcBack.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
                            {}, {}, {}, srcBack);
    }
}
