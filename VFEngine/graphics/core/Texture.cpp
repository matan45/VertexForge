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
    // Resolve TextureCompressionFormat to Vulkan format
    static vk::Format resolveVulkanFormat(resource::TextureCompressionFormat compression, vk::Format uncompressedFormat)
    {
        switch (compression)
        {
            case resource::TextureCompressionFormat::BC7:
            {
                // Map sRGB vs Unorm based on the original format
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
        // If resources were extracted via extractResources(), all handles are null — nothing to do
        if (!image && !imageView && !sampler && !imageMemory && mipImageViews.empty())
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        if (isEditorTexture && descriptorSet)
        {
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }

        for (auto& mipDesc : mipDescriptorSets)
        {
            if (mipDesc)
            {
                ImGui_ImplVulkan_RemoveTexture(mipDesc);
            }
        }
        for (auto& mipView : mipImageViews)
        {
            if (mipView)
            {
                device.getLogicalDevice().destroyImageView(mipView);
            }
        }
        for (auto& mipSamp : mipSamplers)
        {
            if (mipSamp)
            {
                device.getLogicalDevice().destroySampler(mipSamp);
            }
        }

        device.getLogicalDevice().destroyImageView(imageView);
        device.getLogicalDevice().destroyImage(image);
        device.getLogicalDevice().freeMemory(imageMemory);
        device.getLogicalDevice().destroySampler(sampler);
    }

    void Texture::extractResources(DeferredDeletionQueue& queue)
    {
        // Queue image, memory, and primary imageView together
        if (image || imageMemory || imageView)
        {
            std::vector<vk::ImageView> views;
            if (imageView) views.push_back(imageView);
            queue.queueImage(image, imageMemory, views);
        }

        // Queue primary sampler
        if (sampler)
        {
            queue.queueSampler(sampler);
        }

        // Queue mip-level views and samplers
        for (auto& v : mipImageViews)
        {
            if (v) queue.queueImageView(v);
        }
        for (auto& s : mipSamplers)
        {
            if (s) queue.queueSampler(s);
        }

        // Null all handles so destructor becomes a no-op
        image = nullptr;
        imageMemory = nullptr;
        imageView = nullptr;
        sampler = nullptr;
        mipImageViews.clear();
        mipSamplers.clear();
    }

    void Texture::loadHDRFromFile(std::string_view filePath, bool isEditor)
    {
        auto textureData = resource::ResourceManager::loadHDRAsync(asset::AssetRef::fromPath(std::string(filePath)));
        auto texturePtr = textureData.get();

        if (!texturePtr || texturePtr->mipData.empty())
        {
            vfLogError("Failed to load HDR texture from: {}", filePath);
            return;
        }

        imageData.height = texturePtr->height;
        imageData.width = texturePtr->width;
        imageData.numbersOfChannels = texturePtr->numbersOfChannels;
        imageData.mipLevels = texturePtr->mipLevels;

        bool isCompressed = (texturePtr->compressionFormat != resource::TextureCompressionFormat::Uncompressed);
        vk::Format hdrFormat = resolveVulkanFormat(texturePtr->compressionFormat, vk::Format::eR32G32B32A32Sfloat);

        // Calculate total staging buffer size using dataSize
        vk::DeviceSize totalSize = 0;
        for (const auto& mip : texturePtr->mipData)
        {
            totalSize += mip.dataSize;
        }

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferInfo.size = totalSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        BufferUtilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

        // Copy all mip levels to staging buffer
        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, totalSize, {}, &data);
            result != vk::Result::eSuccess)
        {
            vfLogError("failed to map memory");
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingBufferMemory);
            return;
        }

        vk::DeviceSize offset = 0;
        for (const auto& mip : texturePtr->mipData)
        {
            memcpy(static_cast<char*>(data) + offset, mip.data.data(), mip.dataSize);
            offset += mip.dataSize;
        }
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);

        // Create image with all mip levels
        ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageInfo.width = texturePtr->width;
        imageInfo.height = texturePtr->height;
        imageInfo.mipLevels = texturePtr->mipLevels;
        imageInfo.format = hdrFormat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        ImageUtilities::createImage(imageInfo, image, imageMemory);

        // Transition all mip levels to transfer destination
        vk::UniqueCommandBuffer commandTransitionA = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        ImageUtilities::transitionImageLayout(commandTransitionA.get(), image, vk::ImageLayout::eUndefined,
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
                region.imageOffset = vk::Offset3D{0, 0, 0};
                region.imageExtent = vk::Extent3D{mip.width, mip.height, 1};

                regions.push_back(region);
                offset += mip.dataSize;
            }

            copyCommand.get().copyBufferToImage(
                stagingBuffer,
                image,
                vk::ImageLayout::eTransferDstOptimal,
                regions
            );

            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), copyCommand);
        }

        // Transition to shader read
        vk::UniqueCommandBuffer commandTransitionB = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        ImageUtilities::transitionImageLayout(commandTransitionB.get(), image, vk::ImageLayout::eTransferDstOptimal,
                                              vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor,
                                              1, texturePtr->mipLevels);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionB);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        texturePtr->releaseCPUData();

        createSampler(texturePtr->mipLevels);

        core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
        imageViewRequest.format = hdrFormat;
        imageViewRequest.mipLevels = texturePtr->mipLevels;
        ImageUtilities::createImageView(imageViewRequest, imageView);
        if (isEditor)
        {
            isEditorTexture = true;
            descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            createPerMipViews(hdrFormat);
        }
    }

    void Texture::loadHDRFromData(const resource::HDRData& hdrData, bool isEditor)
    {
        if (hdrData.mipData.empty())
        {
            vfLogError("HDR data is empty");
            return;
        }

        imageData.height = hdrData.height;
        imageData.width = hdrData.width;
        imageData.numbersOfChannels = hdrData.numbersOfChannels;
        imageData.mipLevels = hdrData.mipLevels;

        bool isCompressed = (hdrData.compressionFormat != resource::TextureCompressionFormat::Uncompressed);
        vk::Format hdrFormat = resolveVulkanFormat(hdrData.compressionFormat, vk::Format::eR32G32B32A32Sfloat);

        // Calculate total staging buffer size
        vk::DeviceSize totalSize = 0;
        for (const auto& mip : hdrData.mipData)
        {
            totalSize += mip.dataSize;
        }

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferInfo.size = totalSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        BufferUtilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

        // Copy all mip levels to staging buffer
        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, totalSize, {}, &data);
            result != vk::Result::eSuccess)
        {
            vfLogError("failed to map memory");
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingBufferMemory);
            return;
        }

        vk::DeviceSize offset = 0;
        for (const auto& mip : hdrData.mipData)
        {
            memcpy(static_cast<char*>(data) + offset, mip.data.data(), mip.dataSize);
            offset += mip.dataSize;
        }
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);

        // Create image with all mip levels
        ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageInfo.width = hdrData.width;
        imageInfo.height = hdrData.height;
        imageInfo.mipLevels = hdrData.mipLevels;
        imageInfo.format = hdrFormat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        ImageUtilities::createImage(imageInfo, image, imageMemory);

        // Transition to transfer destination
        vk::UniqueCommandBuffer commandTransitionA = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        ImageUtilities::transitionImageLayout(commandTransitionA.get(), image, vk::ImageLayout::eUndefined,
                                              vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor,
                                              1, hdrData.mipLevels);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionA);

        // Copy each mip level
        {
            vk::UniqueCommandBuffer copyCommand = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool.get());

            std::vector<vk::BufferImageCopy> regions;
            regions.reserve(hdrData.mipLevels);
            offset = 0;

            for (uint32_t level = 0; level < hdrData.mipLevels; ++level)
            {
                const auto& mip = hdrData.mipData[level];

                vk::BufferImageCopy region{};
                region.bufferOffset = offset;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                region.imageSubresource.mipLevel = level;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = vk::Offset3D{0, 0, 0};
                region.imageExtent = vk::Extent3D{mip.width, mip.height, 1};

                regions.push_back(region);
                offset += mip.dataSize;
            }

            copyCommand.get().copyBufferToImage(
                stagingBuffer,
                image,
                vk::ImageLayout::eTransferDstOptimal,
                regions
            );

            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), copyCommand);
        }

        // Transition to shader read
        vk::UniqueCommandBuffer commandTransitionB = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        ImageUtilities::transitionImageLayout(commandTransitionB.get(), image, vk::ImageLayout::eTransferDstOptimal,
                                              vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor,
                                              1, hdrData.mipLevels);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionB);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        createSampler(hdrData.mipLevels);

        core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
        imageViewRequest.format = hdrFormat;
        imageViewRequest.mipLevels = hdrData.mipLevels;
        ImageUtilities::createImageView(imageViewRequest, imageView);

        if (isEditor)
        {
            isEditorTexture = true;
            descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            createPerMipViews(hdrFormat);
        }
    }

    void Texture::loadTextureFromFile(std::string_view filePath, vk::Format format, bool isEditor)
    {
        auto textureData = resource::ResourceManager::loadTextureAsync(asset::AssetRef::fromPath(std::string(filePath)));
        auto texturePtr = textureData.get();

        if (!texturePtr || texturePtr->mipData.empty())
        {
            vfLogError("Failed to load texture from: {}", filePath);
            return;
        }

        imageData.height = texturePtr->height;
        imageData.width = texturePtr->width;
        imageData.numbersOfChannels = texturePtr->numbersOfChannels;
        imageData.mipLevels = texturePtr->mipLevels;

        // Resolve format based on compression
        vk::Format resolvedFormat = resolveVulkanFormat(texturePtr->compressionFormat, format);

        // Calculate total staging buffer size using dataSize
        vk::DeviceSize totalSize = 0;
        for (const auto& mip : texturePtr->mipData)
        {
            totalSize += mip.dataSize > 0 ? mip.dataSize
                : static_cast<vk::DeviceSize>(mip.width) * mip.height * 4;
        }

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferInfo.size = totalSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        BufferUtilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

        // Copy all mip levels to staging buffer
        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, totalSize, {}, &data);
            result != vk::Result::eSuccess)
        {
            vfLogError("failed to map memory");
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingBufferMemory);
            return;
        }

        vk::DeviceSize offset = 0;
        for (const auto& mip : texturePtr->mipData)
        {
            size_t mipSize = mip.dataSize > 0 ? mip.dataSize
                : static_cast<size_t>(mip.width) * mip.height * 4;
            memcpy(static_cast<char*>(data) + offset, mip.data.data(), mipSize);
            offset += mipSize;
        }
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);

        // Create image with all mip levels
        ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageInfo.width = texturePtr->width;
        imageInfo.height = texturePtr->height;
        imageInfo.mipLevels = texturePtr->mipLevels;
        imageInfo.format = resolvedFormat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        ImageUtilities::createImage(imageInfo, image, imageMemory);

        // Transition all mip levels to transfer destination
        vk::UniqueCommandBuffer commandTransitionA = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        ImageUtilities::transitionImageLayout(commandTransitionA.get(), image, vk::ImageLayout::eUndefined,
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
                offset += mip.dataSize > 0 ? mip.dataSize
                    : static_cast<vk::DeviceSize>(mip.width) * mip.height * 4;
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
        ImageUtilities::transitionImageLayout(commandTransitionB.get(), image, vk::ImageLayout::eTransferDstOptimal,
                                              vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor,
                                              1, texturePtr->mipLevels);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionB);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        // Release CPU mip data now that it's uploaded to GPU
        texturePtr->releaseCPUData();

        createSampler(texturePtr->mipLevels);
        core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
        imageViewRequest.format = resolvedFormat;
        imageViewRequest.mipLevels = texturePtr->mipLevels;
        ImageUtilities::createImageView(imageViewRequest, imageView);
        if (isEditor)
        {
            isEditorTexture = true;
            descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            createPerMipViews(resolvedFormat);
        }
    }

    void Texture::loadTextureFromData(const resource::TextureData& textureData, vk::Format format, bool isEditor)
    {
        if (textureData.mipData.empty())
        {
            vfLogError("Texture data is empty");
            return;
        }

        imageData.height = textureData.height;
        imageData.width = textureData.width;
        imageData.numbersOfChannels = textureData.numbersOfChannels;
        imageData.mipLevels = textureData.mipLevels;

        // Resolve format based on compression
        vk::Format resolvedFormat = resolveVulkanFormat(textureData.compressionFormat, format);

        // Calculate total staging buffer size using dataSize
        vk::DeviceSize totalSize = 0;
        for (const auto& mip : textureData.mipData)
        {
            totalSize += mip.dataSize > 0 ? mip.dataSize
                : static_cast<vk::DeviceSize>(mip.width) * mip.height * 4;
        }

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferInfo.size = totalSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        BufferUtilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

        // Copy all mip levels to staging buffer
        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, totalSize, {}, &data);
            result != vk::Result::eSuccess)
        {
            vfLogError("failed to map memory");
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingBufferMemory);
            return;
        }

        vk::DeviceSize offset = 0;
        for (const auto& mip : textureData.mipData)
        {
            size_t mipSize = mip.dataSize > 0 ? mip.dataSize
                : static_cast<size_t>(mip.width) * mip.height * 4;
            memcpy(static_cast<char*>(data) + offset, mip.data.data(), mipSize);
            offset += mipSize;
        }
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);

        // Create image with all mip levels
        ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageInfo.width = textureData.width;
        imageInfo.height = textureData.height;
        imageInfo.mipLevels = textureData.mipLevels;
        imageInfo.format = resolvedFormat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        ImageUtilities::createImage(imageInfo, image, imageMemory);

        // Transition all mip levels to transfer destination
        vk::UniqueCommandBuffer commandTransitionA = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        ImageUtilities::transitionImageLayout(commandTransitionA.get(), image, vk::ImageLayout::eUndefined,
                                              vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor,
                                              1, textureData.mipLevels);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionA);

        vk::UniqueCommandBuffer copyCommand = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());

        std::vector<vk::BufferImageCopy> regions;
        regions.reserve(textureData.mipLevels);
        offset = 0;

        for (uint32_t level = 0; level < textureData.mipLevels; ++level)
        {
            const auto& mip = textureData.mipData[level];

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
            offset += mip.dataSize > 0 ? mip.dataSize
                : static_cast<vk::DeviceSize>(mip.width) * mip.height * 4;
        }

        copyCommand.get().copyBufferToImage(
            stagingBuffer,
            image,
            vk::ImageLayout::eTransferDstOptimal,
            regions
        );

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), copyCommand);

        // Transition all mip levels to shader read
        vk::UniqueCommandBuffer commandTransitionB = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        ImageUtilities::transitionImageLayout(commandTransitionB.get(), image, vk::ImageLayout::eTransferDstOptimal,
                                              vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor,
                                              1, textureData.mipLevels);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionB);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        createSampler(textureData.mipLevels);
        core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
        imageViewRequest.format = resolvedFormat;
        imageViewRequest.mipLevels = textureData.mipLevels;
        ImageUtilities::createImageView(imageViewRequest, imageView);
        if (isEditor)
        {
            isEditorTexture = true;
            descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            createPerMipViews(resolvedFormat);
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
        samplerInfo.maxAnisotropy = 16.0f; // Enable max anisotropic filtering
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

    void Texture::createMipSampler(vk::Sampler& outSampler, uint32_t mipLevel)
    {
        using enum vk::SamplerAddressMode;
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eNearest; // No filtering for exact mip view
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
        if (!isEditorTexture || imageData.mipLevels <= 1)
        {
            return; // Only create per-mip views for editor textures with multiple mips
        }

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
            viewInfo.subresourceRange.levelCount = 1; // Only this mip level
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            mipImageViews[mip] = device.getLogicalDevice().createImageView(viewInfo);

            createMipSampler(mipSamplers[mip], mip);

            mipDescriptorSets[mip] = ImGui_ImplVulkan_AddTexture(
                mipSamplers[mip],
                mipImageViews[mip],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }
    }
}
