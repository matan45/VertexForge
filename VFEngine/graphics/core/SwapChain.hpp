#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vector>

namespace core {
	class Device;

	struct SwapChainDepthStencil {
		vk::UniqueImage depthStencilImage;
		vk::UniqueDeviceMemory depthStencilMemory;
		vk::UniqueImageView depthStencilView;
	};

	class SwapChain
	{
	private:
		Device& device;

		vk::UniqueSwapchainKHR swapchain;
		std::vector<vk::Image> swapchainImages;
		std::vector<vk::ImageView> swapchainImageViews;

		SwapChainDepthStencil swapchainDepthStencil{};

		vk::Format swapchainImageFormat{ vk::Format::eUndefined };
		vk::Format swapchainDepthStencilFormat{ vk::Format::eUndefined };
		vk::Extent2D swapchainExtent;
		vk::Extent2D renderExtentOverride{0, 0};

		// Display options applied at (re)creation time. Mailbox preserves the
		// historical default (low-latency, no VSync). MSAA defaults to 1x.
		vk::PresentModeKHR desiredPresentMode{ vk::PresentModeKHR::eMailbox };
		vk::SampleCountFlagBits msaaSamples{ vk::SampleCountFlagBits::e1 };

	public:
		explicit SwapChain(Device& device);
		~SwapChain() = default;

		void init(uint32_t width, uint32_t height);
		void cleanUp();

		uint32_t getImageCount() const { return static_cast<uint32_t>(swapchainImages.size()); }

		const vk::SwapchainKHR& getSwapchain() const { return swapchain.get(); }
		const vk::Image& getSwapchainImage(uint32_t imageIndex) const { return swapchainImages[imageIndex]; }
		const vk::ImageView& getSwapchainImageView(uint32_t imageIndex) const { return swapchainImageViews[imageIndex]; }
		const vk::Image& getDepthStencilImage() const { return swapchainDepthStencil.depthStencilImage.get(); }

		vk::Format getSwapchainDepthStencilFormat() const { return swapchainDepthStencilFormat; }
		vk::Format getSwapchainImageFormat() const { return swapchainImageFormat; }
		vk::Format getSceneColorFormat() const { return vk::Format::eR16G16B16A16Sfloat; }

		/// Returns the active rendering extent. When upscaling is active, this returns
		/// the internal render resolution. Otherwise returns the native swapchain extent.
		vk::Extent2D getSwapchainExtent() const
		{
			return renderExtentOverride.width > 0 ? renderExtentOverride : swapchainExtent;
		}

		/// Returns the true display/window resolution (always the native swapchain extent).
		vk::Extent2D getDisplayExtent() const { return swapchainExtent; }

		/// Override the extent returned by getSwapchainExtent() for resolution split.
		/// Pass {0,0} to clear the override.
		void setRenderExtentOverride(vk::Extent2D extent) { renderExtentOverride = extent; }

		/// Desired swapchain present mode (VSync). Applied on the next (re)create.
		/// choosePresentMode() falls back to FIFO when the requested mode is unsupported.
		void setDesiredPresentMode(vk::PresentModeKHR mode) { desiredPresentMode = mode; }
		vk::PresentModeKHR getDesiredPresentMode() const { return desiredPresentMode; }

		/// Active MSAA sample count for the scene pass. Single source of truth read
		/// by all scene pipelines and the offscreen render targets. Clamped to device
		/// limits by the caller (see Device framebuffer sample-count limits).
		void setMSAASamples(vk::SampleCountFlagBits samples) { msaaSamples = samples; }
		vk::SampleCountFlagBits getMSAASamples() const { return msaaSamples; }

		void recreate(uint32_t width, uint32_t height);

	private:
		void createSwapchain(uint32_t width, uint32_t height);
		void createImageViews();
		void createDepthStencil();
		vk::Format findDepthStencilFormat() const;

		vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats) const;
		vk::PresentModeKHR choosePresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes) const;
		vk::Extent2D chooseSwapExtent(uint32_t width, uint32_t height, const vk::SurfaceCapabilitiesKHR& capabilities) const;
	};
}

