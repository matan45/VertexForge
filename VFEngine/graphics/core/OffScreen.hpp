#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include "VulkanMemoryManager.hpp"
#include "GraphicsConstants.hpp"
#include <array>

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

	struct PrevFrameDepthImage {
		vk::Image image;
		VulkanAllocation allocation;
		vk::ImageView imageView;    // Depth-only sampled view
	};

	struct ExposureImage {
		vk::Image image;
		VulkanAllocation allocation;
		vk::ImageView imageView;    // 1x1 R32_SFLOAT for Streamline exposure tag
	};

	struct OffscreenResources {
		std::vector<core::ColorImage> colorImages;       // Scene color (render resolution)
		std::vector<core::ColorImage> displayColorImages; // Display-res output (only when upscaling)
		core::DepthImage depthImage;
		core::StencilImage uiStencilImage;

		// Upscaling resources (created when upscaling is enabled)
		MotionVectorImage motionVectors;
		ReactiveMaskImage reactiveMask;
		UpscaleOutputImage upscaleOutput;
		ExposureImage exposureImage;
		bool upscaleResourcesCreated = false;

		// Previous-frame depth copies for async compute motion vectors
		std::array<PrevFrameDepthImage, MAX_FRAMES_IN_FLIGHT> prevFrameDepth;
		bool prevFrameDepthCreated = false;
	};
}
