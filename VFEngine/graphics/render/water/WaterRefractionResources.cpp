#include "WaterRefractionResources.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"

namespace render::water
{
    WaterRefractionResources::WaterRefractionResources(core::Device& device)
        : device(device)
    {
    }

    WaterRefractionResources::~WaterRefractionResources()
    {
        cleanup();
    }

    void WaterRefractionResources::init(vk::Format swapchainFormat, uint32_t width, uint32_t height, vk::ImageView sceneDepthView)
    {
        createRefractionImage(swapchainFormat, width, height);
        createSampler();
        createDescriptorLayout();
        createDescriptorPool();
        allocateDescriptorSet();
        updateDescriptorSet(sceneDepthView);
        transitionImageInitial();
        initialized = true;
    }

    void WaterRefractionResources::recreate(vk::Format swapchainFormat, uint32_t width, uint32_t height, vk::ImageView sceneDepthView)
    {
        cleanup();
        init(swapchainFormat, width, height, sceneDepthView);
    }

    void WaterRefractionResources::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (descriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(descriptorSetLayout); descriptorSetLayout = nullptr; }
        if (refractionSampler)   { vkDevice.destroySampler(refractionSampler); refractionSampler = nullptr; }
        if (refractionView)      { vkDevice.destroyImageView(refractionView); refractionView = nullptr; }
        if (refractionImage)     { vkDevice.destroyImage(refractionImage); refractionImage = nullptr; }
        if (refractionAllocation) { device.getMemoryManager().free(refractionAllocation); refractionAllocation = {}; }

        initialized = false;
    }

    void WaterRefractionResources::createRefractionImage(vk::Format format, uint32_t width, uint32_t height)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
            width, height, 1, 1,
            format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::ImageUtilities::createImage(req, refractionImage, refractionAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest viewReq(vkDevice, refractionImage,
            format,
            vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
        core::ImageUtilities::createImageView(viewReq, refractionView);
    }

    void WaterRefractionResources::createSampler()
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

        refractionSampler = device.getLogicalDevice().createSampler(info);
    }

    void WaterRefractionResources::createDescriptorLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void WaterRefractionResources::createDescriptorPool()
    {
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void WaterRefractionResources::allocateDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void WaterRefractionResources::updateDescriptorSet(vk::ImageView sceneDepthView)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorImageInfo, 2> imageInfos{};
        imageInfos[0].sampler = refractionSampler;
        imageInfos[0].imageView = refractionView;
        imageInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        // Binding 1: use the same refraction color texture as a dummy
        // (depth sampling removed - water shader uses gl_FragCoord.z directly)
        imageInfos[1].sampler = refractionSampler;
        imageInfos[1].imageView = refractionView;
        imageInfos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo = &imageInfos[0];

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].pImageInfo = &imageInfos[1];

        vkDevice.updateDescriptorSets(writes, nullptr);
    }

    void WaterRefractionResources::transitionImageInitial()
    {
        auto cmd = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), device.getStagingCommandPool());

        core::ImageUtilities::transitionImageLayout(
            cmd.get(), refractionImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(device, cmd);
    }

    void WaterRefractionResources::copySceneColor(vk::CommandBuffer cmd, vk::Image srcColorImage, uint32_t width, uint32_t height)
    {
        // 1. Transition refraction image: ShaderReadOnly -> TransferDst
        vk::ImageMemoryBarrier toTransferDst{};
        toTransferDst.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toTransferDst.newLayout = vk::ImageLayout::eTransferDstOptimal;
        toTransferDst.image = refractionImage;
        toTransferDst.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        toTransferDst.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        toTransferDst.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                            vk::PipelineStageFlagBits::eTransfer,
                            {}, {}, {}, toTransferDst);

        // 2. Transition source: ColorAttachmentOptimal -> TransferSrc
        vk::ImageMemoryBarrier srcToTransfer{};
        srcToTransfer.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
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
        copyRegion.extent = vk::Extent3D {width, height, 1};
        cmd.copyImage(srcColorImage, vk::ImageLayout::eTransferSrcOptimal,
                      refractionImage, vk::ImageLayout::eTransferDstOptimal,
                      copyRegion);

        // 4. Transition refraction: TransferDst -> ShaderReadOnly
        vk::ImageMemoryBarrier toShaderRead{};
        toShaderRead.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        toShaderRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toShaderRead.image = refractionImage;
        toShaderRead.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        toShaderRead.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        toShaderRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eFragmentShader,
                            {}, {}, {}, toShaderRead);

        // 5. Transition source: TransferSrc -> ColorAttachment
        vk::ImageMemoryBarrier srcBack{};
        srcBack.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        srcBack.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        srcBack.image = srcColorImage;
        srcBack.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        srcBack.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        srcBack.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eColorAttachmentOutput,
                            {}, {}, {}, srcBack);
    }
}
