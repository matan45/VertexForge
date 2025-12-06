#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <memory>

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

	class RenderManager
	{
	private:
		Device& device;
		SwapChain& swapChain;
		const window::Window* window;  // Non-owning pointer
		std::unique_ptr<CommandPool> commandPool;
		std::unique_ptr<imguiPass::ImguiRender> imguiRender;

		vk::Semaphore imageAvailableSemaphore;
		vk::Semaphore renderFinishedSemaphore;
		vk::Fence renderFence;

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

		void present() const;
	};
}


