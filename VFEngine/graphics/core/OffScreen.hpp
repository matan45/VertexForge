#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

namespace core {
	struct ColorImage {
		vk::Image colorImage;
		vk::DeviceMemory colorImageMemory;
		vk::ImageView colorImageView;
		vk::DescriptorSet descriptorSet;
	};

	struct DepthImage {
		vk::Image depthImage;
		vk::DeviceMemory depthImageMemory;
		vk::ImageView depthImageView;
	};

	struct StencilImage {
		vk::Image stencilImage;
		vk::DeviceMemory stencilImageMemory;
		vk::ImageView stencilImageView;
	};

	struct OffscreenResources {
		std::vector<core::ColorImage> colorImages;
		core::DepthImage depthImage;
		core::StencilImage uiStencilImage;
	};
}
