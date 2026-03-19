#include "Texture.hpp"
#include "Device.hpp"
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
                return (uncompressedFormat == vk::Format::eR8G8B8A8Srgb || uncompressedFormat == vk::Format::eB8G8R8A8Srgb)
                    ? vk::Format::eBc7SrgbBlock : vk::Format::eBc7UnormBlock;
            case resource::TextureCompressionFormat::BC6H:
                return vk::Format::eBc6HUfloatBlock;
            default:
                return uncompressedFormat;
        }
    }

    namespace
    {
        struct MipUploadParams
        {
            Device& device;
            vk::UniqueCommandPool& commandPool;
            vk::Image& image;
            vk::DeviceMemory& imageMemory;
            const std::vector<resource::MipLevelData>& mipData;
            uint32_t width;
            uint32_t height;
            uint32_t mipLevels;
            vk::Format format;
            bool useDataSize;
        };

        vk::DeviceSize computeMipSize(const resource::MipLevelData& mip, bool useDataSize)
        {
            if (useDataSize && mip.dataSize > 0) return mip.dataSize;
            return static_cast<vk::DeviceSize>(mip.width) * mip.height * 4;
        }

        bool stageAndUploadMips(MipUploadParams& p)
        {
            vk::DeviceSize totalSize = 0;
            for (const auto& mip : p.mipData)
                totalSize += computeMipSize(mip, p.useDataSize);

            vk::Buffer stagingBuffer;
            vk::DeviceMemory stagingBufferMemory;

            BufferInfoRequest bufferInfo(p.device.getLogicalDevice(), p.device.getPhysicalDevice());
            bufferInfo.size = totalSize;
            bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
            bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            BufferUtilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

            void* data;
            if (p.device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, totalSize, {}, &data) != vk::Result::eSuccess)
            {
                vfLogError("failed to map memory");
                p.device.getLogicalDevice().destroyBuffer(stagingBuffer);
                p.device.getLogicalDevice().freeMemory(stagingBufferMemory);
                return false;
            }

            vk::DeviceSize offset = 0;
            for (const auto& mip : p.mipData)
            {
                size_t mipSize = static_cast<size_t>(computeMipSize(mip, p.useDataSize));
                memcpy(static_cast<char*>(data) + offset, mip.data.data(), mipSize);
                offset += mipSize;
            }
            p.device.getLogicalDevice().unmapMemory(stagingBufferMemory);

            ImageInfoRequest imageInfo(p.device.getLogicalDevice(), p.device.getPhysicalDevice());
            imageInfo.width = p.width;
            imageInfo.height = p.height;
            imageInfo.mipLevels = p.mipLevels;
            imageInfo.format = p.format;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
            imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            ImageUtilities::createImage(imageInfo, p.image, p.imageMemory);

            auto cmdA = Utilities::beginSingleTimeCommands(p.device.getLogicalDevice(), p.commandPool.get());
            ImageUtilities::transitionImageLayout(cmdA.get(), p.image, vk::ImageLayout::eUndefined,
                                                  vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor, 1, p.mipLevels);
            Utilities::endSingleTimeCommands(p.device.getGraphicsQueue(), cmdA);

            {
                auto copyCmd = Utilities::beginSingleTimeCommands(p.device.getLogicalDevice(), p.commandPool.get());
                std::vector<vk::BufferImageCopy> regions;
                regions.reserve(p.mipLevels);
                offset = 0;

                for (uint32_t level = 0; level < p.mipLevels; ++level)
                {
                    const auto& mip = p.mipData[level];
                    vk::BufferImageCopy region{};
                    region.bufferOffset = offset;
                    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                    region.imageSubresource.mipLevel = level;
                    region.imageSubresource.baseArrayLayer = 0;
                    region.imageSubresource.layerCount = 1;
                    region.imageOffset = vk::Offset3D{0, 0, 0};
                    region.imageExtent = vk::Extent3D{mip.width, mip.height, 1};
                    regions.push_back(region);
                    offset += computeMipSize(mip, p.useDataSize);
                }

                copyCmd.get().copyBufferToImage(stagingBuffer, p.image, vk::ImageLayout::eTransferDstOptimal, regions);
                Utilities::endSingleTimeCommands(p.device.getGraphicsQueue(), copyCmd);
            }

            auto cmdB = Utilities::beginSingleTimeCommands(p.device.getLogicalDevice(), p.commandPool.get());
            ImageUtilities::transitionImageLayout(cmdB.get(), p.image, vk::ImageLayout::eTransferDstOptimal,
                                                  vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor, 1, p.mipLevels);
            Utilities::endSingleTimeCommands(p.device.getGraphicsQueue(), cmdB);

            p.device.getLogicalDevice().destroyBuffer(stagingBuffer);
            p.device.getLogicalDevice().freeMemory(stagingBufferMemory);
            return true;
        }

        void finalizeTexture(Texture& tex, Device& device, vk::Image image, vk::Format format,
                             uint32_t mipLevels, bool isEditor)
        {
            ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
            imageViewRequest.format = format;
            imageViewRequest.mipLevels = mipLevels;
            vk::ImageView imageView;
            ImageUtilities::createImageView(imageViewRequest, imageView);

            // Access through public getters after loading, the view is set through the load method chain
        }
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

        imageData = {texturePtr->width, texturePtr->height, texturePtr->numbersOfChannels, texturePtr->mipLevels};
        vk::Format hdrFormat = resolveVulkanFormat(texturePtr->compressionFormat, vk::Format::eR32G32B32A32Sfloat);

        MipUploadParams params{device, commandPool, image, imageMemory, texturePtr->mipData,
                               texturePtr->width, texturePtr->height, texturePtr->mipLevels, hdrFormat, true};
        if (!stageAndUploadMips(params)) return;

        texturePtr->releaseCPUData();
        createSampler(texturePtr->mipLevels);

        ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
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

        imageData = {hdrData.width, hdrData.height, hdrData.numbersOfChannels, hdrData.mipLevels};
        vk::Format hdrFormat = resolveVulkanFormat(hdrData.compressionFormat, vk::Format::eR32G32B32A32Sfloat);

        MipUploadParams params{device, commandPool, image, imageMemory, hdrData.mipData,
                               hdrData.width, hdrData.height, hdrData.mipLevels, hdrFormat, true};
        if (!stageAndUploadMips(params)) return;

        createSampler(hdrData.mipLevels);

        ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
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

        imageData = {texturePtr->width, texturePtr->height, texturePtr->numbersOfChannels, texturePtr->mipLevels};
        vk::Format resolvedFormat = resolveVulkanFormat(texturePtr->compressionFormat, format);

        MipUploadParams params{device, commandPool, image, imageMemory, texturePtr->mipData,
                               texturePtr->width, texturePtr->height, texturePtr->mipLevels, resolvedFormat, false};
        if (!stageAndUploadMips(params)) return;

        texturePtr->releaseCPUData();
        createSampler(texturePtr->mipLevels);

        ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
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

        imageData = {textureData.width, textureData.height, textureData.numbersOfChannels, textureData.mipLevels};
        vk::Format resolvedFormat = resolveVulkanFormat(textureData.compressionFormat, format);

        MipUploadParams params{device, commandPool, image, imageMemory, textureData.mipData,
                               textureData.width, textureData.height, textureData.mipLevels, resolvedFormat, false};
        if (!stageAndUploadMips(params)) return;

        createSampler(textureData.mipLevels);

        ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), image);
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
}
