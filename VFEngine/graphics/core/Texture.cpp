#include "Texture.hpp"
#include "Device.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Logger.hpp"
#include <imgui_impl_vulkan.h>

namespace core {
	Texture::Texture(Device& device) :device{ device }
	{
		vk::CommandPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
		poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();

		commandPool = device.getLogicalDevice().createCommandPoolUnique(poolInfo);
	}

	Texture::~Texture()
	{
		device.getLogicalDevice().waitIdle();
		device.getLogicalDevice().destroyImageView(imageView);
		device.getLogicalDevice().destroyImage(image);
		device.getLogicalDevice().freeMemory(imageMemory);
		device.getLogicalDevice().destroySampler(sampler);
	}

	void Texture::loadFromFile(std::string_view filePath,bool isEditor)
	{
		auto textureData = resource::ResourceManager::loadTextureAsync(filePath);
		auto texturePtr = textureData.get();
		uint32_t imageWidth = texturePtr->width;
		uint32_t imageHeight = texturePtr->height;
		vk::DeviceSize imageSize = imageWidth * imageHeight * 4;

		vk::Buffer stagingBuffer;
		vk::DeviceMemory stagingBufferMemory;

		BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
		bufferInfo.size = imageSize;
		bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
		Utilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

		void* data;
		if (vk::Result result = device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, imageSize, {}, &data); result != vk::Result::eSuccess) {
			loggerError("failed to map memory");
		}
		memcpy(data, texturePtr->textureData.data(), imageSize);
		device.getLogicalDevice().unmapMemory(stagingBufferMemory);


		ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
		imageInfo.width = imageWidth;
		imageInfo.height = imageHeight;
		imageInfo.format = vk::Format::eR8G8B8A8Srgb;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
		imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
		Utilities::createImage(imageInfo, image, imageMemory);

		vk::UniqueCommandBuffer commandTransitionA = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
		Utilities::transitionImageLayout(commandTransitionA.get(), image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor);
		Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionA);

		copyBufferToImage(stagingBuffer, imageWidth, imageHeight);

		vk::UniqueCommandBuffer commandTransitionB = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
		Utilities::transitionImageLayout(commandTransitionB.get(), image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
		Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandTransitionB);


		device.getLogicalDevice().destroyBuffer(stagingBuffer);
		device.getLogicalDevice().freeMemory(stagingBufferMemory);

		createSampler();
		core::ImageViewInfoRequest imageDepthRequest(device.getLogicalDevice(), image);
		imageDepthRequest.format = vk::Format::eR8G8B8A8Srgb;
		Utilities::createImageView(imageDepthRequest, imageView);
		if (isEditor) {
			descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		
	}

	void Texture::createSampler()
	{
		using enum vk::SamplerAddressMode;
		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = eRepeat;
		samplerInfo.addressModeV = eRepeat;
		samplerInfo.addressModeW = eRepeat;
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = 16;
		samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		sampler = device.getLogicalDevice().createSampler(samplerInfo);
	}

	void Texture::copyBufferToImage(vk::Buffer buffer, uint32_t width, uint32_t height)
	{
		vk::UniqueCommandBuffer command = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());

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
