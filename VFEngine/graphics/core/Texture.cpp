#include "Texture.hpp"
#include "Device.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Logger.hpp"
#include <imgui_impl_vulkan.h>
#include <vector>

namespace core
{
    Texture::Texture(Device& device) : device{device}
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();

        commandPool = device.getLogicalDevice().createCommandPoolUnique(poolInfo);
    }

    Texture::~Texture()
    {
        device.getLogicalDevice().waitIdle();

        // Remove ImGui descriptor set if this was an editor texture
        if (isEditorTexture && descriptorSet) {
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }

        device.getLogicalDevice().destroyImageView(imageView);
        device.getLogicalDevice().destroyImage(image);
        device.getLogicalDevice().freeMemory(imageMemory);
        device.getLogicalDevice().destroySampler(sampler);
    }

    void Texture::loadHDRFromFile(std::string_view filePath, bool isEditor)
    {
        auto textureData = resource::ResourceManager::loadHDRAsync(filePath);
        auto texturePtr = textureData.get();

        if (!texturePtr || texturePtr->mipData.empty())
        {
            loggerError("Failed to load HDR texture from: {}", filePath);
            return;
        }

        imageData.height = texturePtr->height;
        imageData.width = texturePtr->width;
        imageData.numbersOfChannels = texturePtr->numbersOfChannels;
        imageData.mipLevels = texturePtr->mipLevels;

        // Calculate total staging buffer size for all mip levels
        vk::DeviceSize totalSize = 0;
        for (const auto& mip : texturePtr->mipData)
        {
            totalSize += static_cast<vk::DeviceSize>(mip.width) * mip.height * 4 * sizeof(float);
        }

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferInfo.size = totalSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        Utilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

        // Copy all mip levels to staging buffer
        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, totalSize, {}, &data);
            result != vk::Result::eSuccess)
        {
            loggerError("failed to map memory");
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingBufferMemory);
            return;
        }

        vk::DeviceSize offset = 0;
        for (const auto& mip : texturePtr->mipData)
        {
            size_t mipSize = static_cast<size_t>(mip.width) * mip.height * 4 * sizeof(float);
            memcpy(static_cast<char*>(data) + offset, mip.data.data(), mipSize);
            offset += mipSize;
        }
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);

        // Create image with all mip levels
        ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageInfo.width = texturePtr->width;
        imageInfo.height = texturePtr->height;
        imageInfo.mipLevels = texturePtr->mipLevels;
        imageInfo.format = vk::Format::eR32G32B32A32Sfloat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        Utilities::createImage(imageInfo, image, imageMemory);

        // Transition all mip levels to transfer destination
        vk::UniqueCommandBuffer commandTransitionA = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        Utilities::transitionImageLayout(commandTransitionA.get(), image, vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor,
                                         1, texturePtr->mipLevels);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionA);

        // Copy each mip level from staging buffer to image
        {
            vk::UniqueCommandBuffer copyCommand = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool.get());

            std::vector<vk::BufferImageCopy> regions;
            regions.reserve(texturePtr->mipLevels);
            offset = 0;

            for (uint32_t level = 0; level < texturePtr->mipLevels; ++level)
            {
                const auto& mip = texturePtr->mipData[level];

                vk::BufferImageCopy region{};
                region.bufferOffset = offset;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                region.imageSubresource.mipLevel = level;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = vk::Offset3D(0, 0, 0);
                region.imageExtent = vk::Extent3D(mip.width, mip.height, 1);

                regions.push_back(region);
                offset += static_cast<vk::DeviceSize>(mip.width) * mip.height * 4 * sizeof(float);
            }

            copyCommand.get().copyBufferToImage(
                stagingBuffer,
                image,
                vk::ImageLayout::eTransferDstOptimal,
                regions
            );

            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), copyCommand);
        }

        // Transition all mip levels to shader read
        vk::UniqueCommandBuffer commandTransitionB = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        Utilities::transitionImageLayout(commandTransitionB.get(), image, vk::ImageLayout::eTransferDstOptimal,
                                         vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor,
                                         1, texturePtr->mipLevels);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionB);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        createSampler(texturePtr->mipLevels);

        core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
        imageViewRequest.format = vk::Format::eR32G32B32A32Sfloat;
        imageViewRequest.mipLevels = texturePtr->mipLevels;
        Utilities::createImageView(imageViewRequest, imageView);
        if (isEditor)
        {
            isEditorTexture = true;
            descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void Texture::loadTextureFromFile(std::string_view filePath, vk::Format format, bool isEditor)
    {
        auto textureData = resource::ResourceManager::loadTextureAsync(filePath);
        auto texturePtr = textureData.get();

        if (!texturePtr || texturePtr->mipData.empty())
        {
            loggerError("Failed to load texture from: {}", filePath);
            return;
        }

        imageData.height = texturePtr->height;
        imageData.width = texturePtr->width;
        imageData.numbersOfChannels = texturePtr->numbersOfChannels;
        imageData.mipLevels = texturePtr->mipLevels;

        // Calculate total staging buffer size for all mip levels
        vk::DeviceSize totalSize = 0;
        for (const auto& mip : texturePtr->mipData)
        {
            totalSize += static_cast<vk::DeviceSize>(mip.width) * mip.height * 4 * sizeof(unsigned char);
        }

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferInfo.size = totalSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        Utilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

        // Copy all mip levels to staging buffer
        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, totalSize, {}, &data);
            result != vk::Result::eSuccess)
        {
            loggerError("failed to map memory");
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingBufferMemory);
            return;
        }

        vk::DeviceSize offset = 0;
        for (const auto& mip : texturePtr->mipData)
        {
            size_t mipSize = static_cast<size_t>(mip.width) * mip.height * 4;
            memcpy(static_cast<char*>(data) + offset, mip.data.data(), mipSize);
            offset += mipSize;
        }
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);

        // Create image with all mip levels
        ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageInfo.width = texturePtr->width;
        imageInfo.height = texturePtr->height;
        imageInfo.mipLevels = texturePtr->mipLevels;
        imageInfo.format = format;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        Utilities::createImage(imageInfo, image, imageMemory);

        // Transition all mip levels to transfer destination
        vk::UniqueCommandBuffer commandTransitionA = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        Utilities::transitionImageLayout(commandTransitionA.get(), image, vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor,
                                         1, texturePtr->mipLevels);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionA);

        // Copy each mip level from staging buffer to image
        {
            vk::UniqueCommandBuffer copyCommand = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool.get());

            std::vector<vk::BufferImageCopy> regions;
            regions.reserve(texturePtr->mipLevels);
            offset = 0;

            for (uint32_t level = 0; level < texturePtr->mipLevels; ++level)
            {
                const auto& mip = texturePtr->mipData[level];

                vk::BufferImageCopy region{};
                region.bufferOffset = offset;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                region.imageSubresource.mipLevel = level;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = vk::Offset3D(0, 0, 0);
                region.imageExtent = vk::Extent3D(mip.width, mip.height, 1);

                regions.push_back(region);
                offset += static_cast<vk::DeviceSize>(mip.width) * mip.height * 4;
            }

            copyCommand.get().copyBufferToImage(
                stagingBuffer,
                image,
                vk::ImageLayout::eTransferDstOptimal,
                regions
            );

            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), copyCommand);
        }

        // Transition all mip levels to shader read
        vk::UniqueCommandBuffer commandTransitionB = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        Utilities::transitionImageLayout(commandTransitionB.get(), image, vk::ImageLayout::eTransferDstOptimal,
                                         vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor,
                                         1, texturePtr->mipLevels);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionB);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        createSampler(texturePtr->mipLevels);
        core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
        imageViewRequest.format = format;
        imageViewRequest.mipLevels = texturePtr->mipLevels;
        Utilities::createImageView(imageViewRequest, imageView);
        if (isEditor)
        {
            isEditorTexture = true;
            descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
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
        samplerInfo.maxAnisotropy = 16.0f;  // Enable max anisotropic filtering
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        // Mipmap settings for trilinear filtering
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
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D(0, 0, 0);
        region.imageExtent = vk::Extent3D(width, height, 1);

        command.get().copyBufferToImage(
            buffer,
            image,
            vk::ImageLayout::eTransferDstOptimal,
            region
        );

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), command);
    }
}
