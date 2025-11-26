#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <imgui.h>
#include <vector>

namespace core{
	class Device;
	class SwapChain;
	class CommandPool;
}

namespace window {
	class Window;
}

namespace imguiPass {
	class ImguiRender
	{
	private:
		core::Device& device;
		core::SwapChain& swapChain;
		core::CommandPool& commandPool;
		const window::Window* window;

		vk::DescriptorPool imGuiDescriptorPool;
		vk::RenderPass imGuiRenderPass;
		std::vector<vk::Framebuffer> imGuiFrameBuffers;

	public:
		explicit ImguiRender(core::Device& device, core::SwapChain& swapChain, core::CommandPool& commandPool,const window::Window* window);
		~ImguiRender() = default;

		void init();
		void cleanUp() const;
		void recreate();
		void render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

	private:
		
		void theme() const;
		void createRenderPass();
		void createDescriptorPool();
		void createFrameBuffers();

	};
}


