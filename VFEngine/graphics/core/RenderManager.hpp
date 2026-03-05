#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <functional>

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
	class DeferredDeletionQueue;

	constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

	using ResizeCallback = std::function<void()>;

	class RenderManager
	{
	private:
		Device& device;
		SwapChain& swapChain;
		const window::Window* window;  // Non-owning pointer
		bool imguiEnabled;
		std::unique_ptr<CommandPool> commandPool;
		std::unique_ptr<imguiPass::ImguiRender> imguiRender;
		std::unique_ptr<DeferredDeletionQueue> deletionQueue;
		mutable ResizeCallback onResizeCallback;

		// Minimal present pass (used when ImGui is disabled)
		vk::RenderPass presentRenderPass;
		std::vector<vk::Framebuffer> presentFrameBuffers;

		// Per-frame synchronization objects (indexed by currentFrame)
		std::vector<vk::Semaphore> imageAvailableSemaphores;
		std::vector<vk::Fence> inFlightFences;

		// Per-swapchain-image semaphores (indexed by imageIndex)
		std::vector<vk::Semaphore> renderFinishedSemaphores;

		// Track which fence is associated with each swapchain image
		std::vector<vk::Fence> imagesInFlight;

		uint32_t currentFrame = 0;
		inline static uint32_t imageIndex;
		inline static DeferredDeletionQueue* globalDeletionQueue;

		void createPresentPass();
		void createPresentFrameBuffers();
		void cleanUpPresentPass() const;

	public:
		explicit RenderManager(Device& device, SwapChain& swapChain, const window::Window* window, bool imguiEnabled = true);
		~RenderManager();

		void init();

		void render();

		void recreate(uint32_t width, uint32_t height);

		static uint32_t getImageIndex() { return imageIndex; }
		static DeferredDeletionQueue* getGlobalDeletionQueue() { return globalDeletionQueue; }

		void setResizeCallback(ResizeCallback callback) { onResizeCallback = std::move(callback); }

		// Access for systems that need deferred deletion
		DeferredDeletionQueue* getDeletionQueue() { return deletionQueue.get(); }

		void cleanUp() const;

	private:
		void draw(const vk::CommandBuffer& commandBuffer) const;

		void present(uint32_t frameIndex);
	};
}


