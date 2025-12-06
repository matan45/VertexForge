#include "Texture.hpp"
#include "Device.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Logger.hpp"
#include <imgui_impl_vulkan.h>

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

    void Texture::loadHDRFromFile(std::string_view filePath, vk::Format format, bool isEditor)
    {
        auto textureData = resource::ResourceManager::loadHDRAsync(filePath);
        auto texturePtr = textureData.get();
        vk::DeviceSize imageSize = texturePtr->width * texturePtr->height * texturePtr->numbersOfChannels * sizeof(
            float);

        imageData.height = texturePtr->height;
        imageData.width = texturePtr->width;
        imageData.numbersOfChannels = texturePtr->numbersOfChannels;

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferInfo.size = imageSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        Utilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, imageSize, {}, &data);
            result != vk::Result::eSuccess)
        {
            loggerError("failed to map memory");
        }
        memcpy(data, texturePtr->textureData.data(), imageSize);
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);


        ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageInfo.width = texturePtr->width;
        imageInfo.height = texturePtr->height;
        imageInfo.format = format;
        imageInfo.tiling = vk::ImageTiling::eLinear;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        Utilities::createImage(imageInfo, image, imageMemory);

        vk::UniqueCommandBuffer commandTransitionA = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        Utilities::transitionImageLayout(commandTransitionA.get(), image, vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionA);

        copyBufferToImage(stagingBuffer, texturePtr->width, texturePtr->height);

        vk::UniqueCommandBuffer commandTransitionB = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        Utilities::transitionImageLayout(commandTransitionB.get(), image, vk::ImageLayout::eTransferDstOptimal,
                                         vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionB);


        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        createSampler();

        core::ImageViewInfoRequest imageDepthRequest(device.getLogicalDevice(), image);
        imageDepthRequest.format = format;
        Utilities::createImageView(imageDepthRequest, imageView);
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
		vk::DeviceSize imageSize = texturePtr->width * texturePtr->height * 4 * sizeof(
			unsigned char);

        imageData.height = texturePtr->height;
        imageData.width = texturePtr->width;
        imageData.numbersOfChannels = texturePtr->numbersOfChannels;

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferInfo.size = imageSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        Utilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, imageSize, {}, &data);
            result != vk::Result::eSuccess)
        {
            loggerError("failed to map memory");
        }
        memcpy(data, texturePtr->textureData.data(), imageSize);
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);

        ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageInfo.width = texturePtr->width;
        imageInfo.height = texturePtr->height;
        imageInfo.format = format;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        Utilities::createImage(imageInfo, image, imageMemory);

        vk::UniqueCommandBuffer commandTransitionA = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        Utilities::transitionImageLayout(commandTransitionA.get(), image, vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionA);

        copyBufferToImage(stagingBuffer, texturePtr->width, texturePtr->height);

        vk::UniqueCommandBuffer commandTransitionB = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());
        Utilities::transitionImageLayout(commandTransitionB.get(), image, vk::ImageLayout::eTransferDstOptimal,
                                         vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
        Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionB);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        createSampler();
        core::ImageViewInfoRequest imageDepthRequest(device.getLogicalDevice(), image);
        imageDepthRequest.format = format;
        Utilities::createImageView(imageDepthRequest, imageView);
        if (isEditor)
        {
            isEditorTexture = true;
            descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void Texture::createSampler()
    {
        using enum vk::SamplerAddressMode;
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = eRepeat;
        samplerInfo.addressModeV = eRepeat;
        samplerInfo.addressModeW = eRepeat;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 1;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void Texture::copyBufferToImage(vk::Buffer buffer, uint32_t width, uint32_t height)
    {
        vk::UniqueCommandBuffer command = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool.get());

        vk::BufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
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
