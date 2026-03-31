#include "ImageUtilities.hpp"
#include "VulkanMemoryManager.hpp"
#include "BufferUtilities.hpp"
#include "Device.hpp"
#include "Utilities.hpp"
#include "memory/GpuAllocationStats.hpp"
#include <cstring>

namespace core
{
	void ImageUtilities::createImage(const ImageInfoRequest& imageInfo, vk::Image& image,
		VulkanAllocation& allocation, VulkanMemoryManager& memManager)
	{
		vk::ImageCreateInfo imageCreateInfo{};
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.extent.width = imageInfo.width;
		imageCreateInfo.extent.height = imageInfo.height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = imageInfo.mipLevels;
		imageCreateInfo.arrayLayers = imageInfo.layers;
		imageCreateInfo.format = imageInfo.format;
		imageCreateInfo.tiling = imageInfo.tiling;
		imageCreateInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageCreateInfo.usage = imageInfo.usage;
		imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
		imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
		imageCreateInfo.flags = imageInfo.imageFlags;

		image = imageInfo.logicalDevice.createImage(imageCreateInfo);

		vk::MemoryRequirements memRequirements = imageInfo.logicalDevice.getImageMemoryRequirements(image);

		allocation = memManager.allocate(memRequirements, imageInfo.properties, false, GpuResourceType::Image);
		imageInfo.logicalDevice.bindImageMemory(image, allocation.memory, allocation.offset);

		memory::GpuAllocationStats::managedAllocationCount.fetch_add(1, std::memory_order_relaxed);
		memory::GpuAllocationStats::managedAllocatedBytes.fetch_add(memRequirements.size, std::memory_order_relaxed);
	}

	void ImageUtilities::createImageView(const ImageViewInfoRequest& imageInfoView, vk::ImageView& imageView)
	{
		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = imageInfoView.image;
		viewInfo.viewType = imageInfoView.imageType;
		viewInfo.format = imageInfoView.format;
		viewInfo.subresourceRange.aspectMask = imageInfoView.aspectFlags;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = imageInfoView.mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = imageInfoView.layerCount;

		imageView = imageInfoView.logicalDevice.createImageView(viewInfo);
	}

	void ImageUtilities::transitionImageLayout(const vk::CommandBuffer& commandBuffer, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageAspectFlags aspectMask, uint32_t layer, uint32_t numMips)
	{
		using enum vk::AccessFlagBits;
		using enum vk::ImageLayout;
		vk::ImageMemoryBarrier barrier{};
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;

		barrier.subresourceRange.aspectMask = aspectMask;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = numMips;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = layer;

		vk::PipelineStageFlags sourceStage;
		vk::PipelineStageFlags destinationStage;

		if (oldLayout == eUndefined && newLayout == eColorAttachmentOptimal) {
			barrier.srcAccessMask = eNoneKHR;
			barrier.dstAccessMask = eColorAttachmentWrite;
			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}
		else if (oldLayout == eColorAttachmentOptimal && newLayout == eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = eColorAttachmentWrite;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == eShaderReadOnlyOptimal && newLayout == eTransferSrcOptimal) {
			barrier.srcAccessMask = eShaderRead;
			barrier.dstAccessMask = eTransferRead;
			sourceStage = vk::PipelineStageFlagBits::eFragmentShader;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == eUndefined && newLayout == eDepthStencilAttachmentOptimal) {
			barrier.srcAccessMask = eNone;
			barrier.dstAccessMask = eDepthStencilAttachmentWrite;
			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
		}
		else if (oldLayout == eDepthStencilAttachmentOptimal && newLayout == eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = eDepthStencilAttachmentWrite;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eLateFragmentTests;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == eTransferSrcOptimal && newLayout == eColorAttachmentOptimal) {
			barrier.srcAccessMask = eTransferRead;
			barrier.dstAccessMask = eColorAttachmentWrite;
			sourceStage = vk::PipelineStageFlagBits::eTransfer;
			destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}
		else if (oldLayout == eUndefined && newLayout == eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = eNone;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == eUndefined && newLayout == eTransferDstOptimal) {
			barrier.srcAccessMask = eNone;
			barrier.dstAccessMask = eTransferWrite;
			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == eColorAttachmentOptimal && newLayout == eTransferSrcOptimal) {
			barrier.srcAccessMask = eColorAttachmentWrite;
			barrier.dstAccessMask = eTransferRead;
			sourceStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == eTransferDstOptimal && newLayout == eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = eTransferWrite;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eTransfer;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == eShaderReadOnlyOptimal && newLayout == eTransferDstOptimal) {
			barrier.srcAccessMask = eShaderRead;
			barrier.dstAccessMask = eTransferWrite;
			sourceStage = vk::PipelineStageFlagBits::eFragmentShader;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == eDepthStencilAttachmentOptimal && newLayout == eDepthStencilReadOnlyOptimal) {
			barrier.srcAccessMask = eDepthStencilAttachmentWrite;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eLateFragmentTests;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == eDepthStencilReadOnlyOptimal && newLayout == eDepthStencilAttachmentOptimal) {
			barrier.srcAccessMask = eShaderRead;
			barrier.dstAccessMask = eDepthStencilAttachmentWrite;
			sourceStage = vk::PipelineStageFlagBits::eFragmentShader;
			destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
		}
		else if (oldLayout == eShaderReadOnlyOptimal && newLayout == eColorAttachmentOptimal) {
			barrier.srcAccessMask = eShaderRead;
			barrier.dstAccessMask = eColorAttachmentWrite;
			sourceStage = vk::PipelineStageFlagBits::eFragmentShader;
			destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}
		else if (oldLayout == eTransferSrcOptimal && newLayout == eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = eTransferRead;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eTransfer;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == eUndefined && newLayout == eGeneral) {
			barrier.srcAccessMask = eNone;
			barrier.dstAccessMask = eShaderWrite | eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eComputeShader;
		}
		else if (oldLayout == eGeneral && newLayout == eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = eShaderWrite;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eComputeShader;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == eGeneral && newLayout == eTransferSrcOptimal) {
			barrier.srcAccessMask = eShaderWrite;
			barrier.dstAccessMask = eTransferRead;
			sourceStage = vk::PipelineStageFlagBits::eComputeShader;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == eGeneral && newLayout == eTransferDstOptimal) {
			barrier.srcAccessMask = eShaderWrite;
			barrier.dstAccessMask = eTransferWrite;
			sourceStage = vk::PipelineStageFlagBits::eComputeShader;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == eShaderReadOnlyOptimal && newLayout == eDepthStencilAttachmentOptimal) {
			barrier.srcAccessMask = eShaderRead;
			barrier.dstAccessMask = eDepthStencilAttachmentWrite;
			sourceStage = vk::PipelineStageFlagBits::eComputeShader;
			destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
		}

		commandBuffer.pipelineBarrier(
			sourceStage, destinationStage,
			vk::DependencyFlags{},
			nullptr, nullptr, barrier
		);
	}

	void ImageUtilities::uploadStagedPixelData(Device& device, vk::Image image,
		const void* pixelData, vk::DeviceSize imageSize,
		uint32_t width, uint32_t height)
	{
		auto vkDevice = device.getLogicalDevice();
		auto& memManager = device.getMemoryManager();

		vk::Buffer stagingBuffer;
		VulkanAllocation stagingAllocation;
		BufferInfoRequest stagingRequest(vkDevice, device.getPhysicalDevice());
		stagingRequest.size = imageSize;
		stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
		stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
		                            vk::MemoryPropertyFlagBits::eHostCoherent;
		BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingAllocation, memManager);

		std::memcpy(stagingAllocation.mappedPtr, pixelData, imageSize);

		auto cmd = Utilities::beginSingleTimeCommands(vkDevice, device.getStagingCommandPool());

		transitionImageLayout(
			cmd.get(), image,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
			vk::ImageAspectFlagBits::eColor);

		vk::BufferImageCopy region{};
		region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = vk::Extent3D{width, height, 1};

		cmd->copyBufferToImage(stagingBuffer, image,
		                       vk::ImageLayout::eTransferDstOptimal, region);

		transitionImageLayout(
			cmd.get(), image,
			vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageAspectFlagBits::eColor);

		Utilities::endSingleTimeCommands(device, cmd);

		BufferUtilities::destroyBuffer(vkDevice, stagingBuffer, stagingAllocation, memManager);
	}

	vk::Sampler ImageUtilities::createVFXSampler(const vk::Device& device)
	{
		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = vk::CompareOp::eAlways;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		return device.createSampler(samplerInfo);
	}

	vk::Sampler ImageUtilities::createVFXDepthSampler(const vk::Device& device)
	{
		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eNearest;
		samplerInfo.minFilter = vk::Filter::eNearest;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = vk::CompareOp::eAlways;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		return device.createSampler(samplerInfo);
	}
}
