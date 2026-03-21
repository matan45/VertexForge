#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <imgui.h>
#include "ImDrawDataSnapshot.hpp"
#include <mutex>
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

		// Double-buffered draw data snapshots for lock-free main/render thread handoff
		ImDrawDataSnapshot snapshots[2];
		std::atomic<uint32_t> writeIndex{0};   // main thread writes to this
		std::atomic<uint32_t> readIndex{0};    // render thread reads from this

	public:
		explicit ImguiRender(core::Device& device, core::SwapChain& swapChain, core::CommandPool& commandPool,const window::Window* window);
		~ImguiRender() = default;

		void init();
		void cleanUp() const;
		void recreate();
		/// Single-threaded render: calls ImGui::Render() + records GPU commands.
		void render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

		/// Main thread: call ImGui::Render() and snapshot the draw data for the render thread.
		void generateAndSnapshotDrawData();

		/// Render thread: render using the snapshotted draw data.
		void renderSnapshotted(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

		/// Render from externally provided ImDrawData.
		void renderFromSnapshot(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
		                        ImDrawData* snapshotDrawData) const;

		/// Render an empty frame (clear only, no draw data). Used when ImGui data is stale.
		void renderEmpty(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

	private:
		
		void theme() const;
		void createRenderPass();
		void createDescriptorPool();
		void createFrameBuffers();

	};
}


