#include "Texture.hpp"
#include "Device.hpp"
#include "DeferredDeletionQueue.hpp"
#include "BufferUtilities.hpp"
#include "ImageUtilities.hpp"
#include "Utilities.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include "asset/AssetRef.hpp"
#include "print/Log.hpp"
#include <imgui_impl_vulkan.h>
#include <vector>

namespace core
{
    static vk::Format resolveVulkanFormat(resource::TextureCompressionFormat compression, vk::Format uncompressedFormat)
    {
        switch (compression)
        {
            case resource::TextureCompressionFormat::BC7:
            {
                if (uncompressedFormat == vk::Format::eR8G8B8A8Srgb ||
                    uncompressedFormat == vk::Format::eB8G8R8A8Srgb)
                    return vk::Format::eBc7SrgbBlock;
                return vk::Format::eBc7UnormBlock;
            }
            case resource::TextureCompressionFormat::BC6H:
                return vk::Format::eBc6HUfloatBlock;
            default:
                return uncompressedFormat;
        }
    }

    Texture::Texture(Device& device) : device{device}
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        commandPool = device.getLogicalDevice().createCommandPoolUnique(poolInfo);
    }

    Texture::~Texture()
    {
        if (!image && !imageView && !sampler && !imageMemory && mipImageViews.empty())
            return;

        device.getLogicalDevice().waitIdle();

        if (isEditorTexture && descriptorSet)
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);

        for (auto& mipDesc : mipDescriptorSets)
            if (mipDesc) ImGui_ImplVulkan_RemoveTexture(mipDesc);
        for (auto& mipView : mipImageViews)
            if (mipView) device.getLogicalDevice().destroyImageView(mipView);
        for (auto& mipSamp : mipSamplers)
            if (mipSamp) device.getLogicalDevice().destroySampler(mipSamp);

        device.getLogicalDevice().destroyImageView(imageView);
        device.getLogicalDevice().destroyImage(image);
        device.getLogicalDevice().freeMemory(imageMemory);
        device.getLogicalDevice().destroySampler(sampler);
    }

    void Texture::extractResources(DeferredDeletionQueue& queue)
    {
        if (image || imageMemory || imageView)
        {
            std::vector<vk::ImageView> views;
            if (imageView) views.push_back(imageView);
            queue.queueImage(image, imageMemory, views);
        }
        if (sampler) queue.queueSampler(sampler);

        for (auto& v : mipImageViews)
            if (v) queue.queueImageView(v);
        for (auto& s : mipSamplers)
            if (s) queue.queueSampler(s);

        image = nullptr; imageMemory = nullptr; imageView = nullptr; sampler = nullptr;
        mipImageViews.clear(); mipSamplers.clear();
    }

    void Texture::createSampler(uint32_t mipLevels)
    {
        using enum vk::SamplerAddressMode;
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = eRepeat;
        samplerInfo.addressModeV = eRepeat;
        samplerInfo.addressModeW = eRepeat;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void Texture::copyBufferToImage(vk::Buffer buffer, uint32_t width, uint32_t height, uint32_t mipLevel)
    {
        vk::UniqueCommandBuffer command = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());

        vk::BufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D(0, 0, 0);
        region.imageExtent = vk::Extent3D(width, height, 1);

        command.get().copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), command);
    }

    void Texture::createMipSampler(vk::Sampler& outSampler, uint32_t mipLevel)
    {
        using enum vk::SamplerAddressMode;
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.addressModeU = eRepeat;
        samplerInfo.addressModeV = eRepeat;
        samplerInfo.addressModeW = eRepeat;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = static_cast<float>(mipLevel);
        samplerInfo.maxLod = static_cast<float>(mipLevel);

        outSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void Texture::createPerMipViews(vk::Format format)
    {
        if (!isEditorTexture || imageData.mipLevels <= 1) return;

        mipImageViews.resize(imageData.mipLevels);
        mipSamplers.resize(imageData.mipLevels);
        mipDescriptorSets.resize(imageData.mipLevels);

        for (uint32_t mip = 0; mip < imageData.mipLevels; ++mip)
        {
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = image;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.baseMipLevel = mip;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            mipImageViews[mip] = device.getLogicalDevice().createImageView(viewInfo);
            createMipSampler(mipSamplers[mip], mip);
            mipDescriptorSets[mip] = ImGui_ImplVulkan_AddTexture(
                mipSamplers[mip], mipImageViews[mip], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }
}
