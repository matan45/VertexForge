#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include "VulkanMemoryManager.hpp"

namespace core {
	struct ColorImage {
		vk::Image colorImage;
		VulkanAllocation colorImageAllocation;
		vk::ImageView colorImageView;
		vk::DescriptorSet descriptorSet;
	};

	struct DepthImage {
		vk::Image depthImage;
		VulkanAllocation depthImageAllocation;
		vk::ImageView depthImageView;
	};

	struct StencilImage {
		vk::Image stencilImage;
		VulkanAllocation stencilImageAllocation;
		vk::ImageView stencilImageView;
	};

	struct OffscreenResources {
		std::vector<core::ColorImage> colorImages;
		core::DepthImage depthImage;
		core::StencilImage uiStencilImage;
	};
}
