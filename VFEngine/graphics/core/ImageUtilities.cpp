#include "ImageUtilities.hpp"
#include "MemoryUtilities.hpp"

namespace core
{
	void ImageUtilities::createImage(const ImageInfoRequest& imageInfo, vk::Image& image, vk::DeviceMemory& imageMemory)
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
		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = MemoryUtilities::findMemoryType(imageInfo.physicalDevice, memRequirements.memoryTypeBits, imageInfo.properties);

		imageMemory = imageInfo.logicalDevice.allocateMemory(allocInfo);
		imageInfo.logicalDevice.bindImageMemory(image, imageMemory, 0);
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

		commandBuffer.pipelineBarrier(
			sourceStage, destinationStage,
			vk::DependencyFlags{},
			nullptr, nullptr, barrier
		);
	}
}
