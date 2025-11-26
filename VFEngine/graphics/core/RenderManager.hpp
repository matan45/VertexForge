#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

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
		const window::Window* window;
		CommandPool* commandPool{ nullptr };
		imguiPass::ImguiRender* imguiRender{ nullptr };

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


