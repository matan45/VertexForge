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

	struct MotionVectorImage {
		vk::Image image;
		VulkanAllocation allocation;
		vk::ImageView imageView;    // R16G16_SFLOAT storage view
		vk::ImageView sampledView;  // For shader read
	};

	struct ReactiveMaskImage {
		vk::Image image;
		VulkanAllocation allocation;
		vk::ImageView imageView;    // R8_UNORM
	};

	struct UpscaleOutputImage {
		vk::Image image;
		VulkanAllocation allocation;
		vk::ImageView imageView;    // Display-resolution output
		vk::DescriptorSet descriptorSet;
	};

	struct OffscreenResources {
		std::vector<core::ColorImage> colorImages;
		core::DepthImage depthImage;
		core::StencilImage uiStencilImage;

		// Upscaling resources (created when upscaling is enabled)
		MotionVectorImage motionVectors;
		ReactiveMaskImage reactiveMask;
		UpscaleOutputImage upscaleOutput;
		bool upscaleResourcesCreated = false;
	};
}
