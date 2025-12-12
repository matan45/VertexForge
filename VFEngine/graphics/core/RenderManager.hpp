#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>

namespace window {
	class Window;
}

namespace render {
	class RenderPassHandler;
}

namespace imguiPass {
	class ImguiRender;
}

namespace core {
	class Device;
	class SwapChain;
	class CommandPool;

	constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

	class RenderManager
	{
	private:
		Device& device;
		SwapChain& swapChain;
		const window::Window* window;  // Non-owning pointer
		std::unique_ptr<CommandPool> commandPool;
		std::unique_ptr<imguiPass::ImguiRender> imguiRender;

		// Per-frame synchronization objects (indexed by currentFrame)
		std::vector<vk::Semaphore> imageAvailableSemaphores;
		std::vector<vk::Fence> inFlightFences;

		// Per-swapchain-image semaphores (indexed by imageIndex)
		std::vector<vk::Semaphore> renderFinishedSemaphores;

		// Track which fence is associated with each swapchain image
		std::vector<vk::Fence> imagesInFlight;

		uint32_t currentFrame = 0;
		inline static uint32_t imageIndex;

	public:
		explicit RenderManager(Device& device, SwapChain& swapChain,const window::Window* window);
		~RenderManager();

		void init();

		void render();

		void recreate(uint32_t width, uint32_t height) const;

		static uint32_t getImageIndex() { return imageIndex; }

		void cleanUp() const;

	private:
		void draw(const vk::CommandBuffer& commandBuffer) const;

		void present(uint32_t frameIndex) const;
	};
}


