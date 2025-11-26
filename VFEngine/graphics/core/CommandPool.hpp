#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

namespace core {
	class Device;
	class SwapChain;

	class CommandPool {
	private:
		Device& device;
		SwapChain& swapChain;

		vk::UniqueCommandPool commandPool;
		std::vector<vk::UniqueCommandBuffer> commandBuffers;

	public:
		explicit CommandPool(Device& device, SwapChain& swapChain);
		~CommandPool() = default;

		vk::CommandPool getCommandPool() const { return commandPool.get(); }

		void cleanUp();
		void recreate();

		vk::CommandBuffer getCommandBuffer(uint32_t index) const { return commandBuffers[index].get(); }
		void resetCommandBuffer(uint32_t index);

	private:
		void createCommandPool();
		void allocateCommandBuffers();
		
	};
}

