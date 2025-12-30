#pragma once

#include <vulkan/vulkan.hpp>

namespace core
{
	struct ImageInfoRequest
	{
		const vk::Device& logicalDevice;
		const vk::PhysicalDevice& physicalDevice;
		uint32_t width;
		uint32_t height;
		uint32_t layers;
		uint32_t mipLevels;
		vk::Format format;
		vk::ImageTiling tiling;
		vk::ImageUsageFlags usage;
		vk::MemoryPropertyFlags properties;
		vk::ImageCreateFlags imageFlags;

		explicit ImageInfoRequest(
			const vk::Device& logicalDevice,
			const vk::PhysicalDevice& physicalDevice,
			uint32_t width = 1,
			uint32_t height = 1,
			uint32_t layers = 1,
			uint32_t mipLevels = 1,
			vk::Format format = vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling tiling = vk::ImageTiling::eOptimal,
			vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal,
			vk::ImageCreateFlags imageFlags = {}
		)
			: logicalDevice{ logicalDevice },
			physicalDevice{ physicalDevice },
			width{ width },
			height{ height },
			layers{ layers },
			mipLevels{ mipLevels },
			format{ format },
			tiling{ tiling },
			usage{ usage },
			properties{ properties },
			imageFlags{ imageFlags }
		{
		}
	};

	struct ImageViewInfoRequest
	{
		const vk::Device& logicalDevice;
		const vk::Image& image;
		vk::Format format;
		vk::ImageAspectFlags aspectFlags;
		vk::ImageViewType imageType;
		uint32_t layerCount;
		uint32_t mipLevels;

		explicit ImageViewInfoRequest(
			const vk::Device& logicalDevice,
			const vk::Image& image,
			vk::Format format = vk::Format::eR8G8B8A8Unorm,
			vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor,
			vk::ImageViewType imageType = vk::ImageViewType::e2D,
			uint32_t layerCount = 1,
			uint32_t mipLevels = 1
		)
			: logicalDevice{ logicalDevice },
			image{ image },
			format{ format },
			aspectFlags{ aspectFlags },
			imageType{ imageType },
			layerCount{ layerCount },
			mipLevels{ mipLevels }
		{
		}
	};

	class ImageUtilities
	{
	private:
		ImageUtilities() = delete;
		~ImageUtilities() = delete;

	public:
		static void createImage(const ImageInfoRequest& imageInfo, vk::Image& image, vk::DeviceMemory& imageMemory);
		static void createImageView(const ImageViewInfoRequest& imageInfoView, vk::ImageView& imageView);

		static void transitionImageLayout(const vk::CommandBuffer& commandBuffer, vk::Image image,
			vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
			vk::ImageAspectFlags aspectMask, uint32_t layer = 1, uint32_t numMips = 1);
	};
}
