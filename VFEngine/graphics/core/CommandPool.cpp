#include "CommandPool.hpp"
#include "Device.hpp"
#include "SwapChain.hpp"
#include "print/Log.hpp"

namespace core {

	CommandPool::CommandPool(Device& device, SwapChain& swapChain) : device{ device }, swapChain{ swapChain } {
		createCommandPool();
		allocateCommandBuffers();
	}

	void CommandPool::allocateCommandBuffers() {
		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = commandPool.get();  // Using vk::UniqueCommandPool
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = swapChain.getImageCount();

		try {
			commandBuffers = device.getLogicalDevice().allocateCommandBuffersUnique(allocInfo);
		}
		catch (const vk::SystemError& err) {
			vfLogError("Failed to allocate command buffers: {}", err.what());
		}
	}

	void CommandPool::createCommandPool() {
		vk::CommandPoolCreateInfo poolInfo{};
		poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
		poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient;

		try {
			commandPool = device.getLogicalDevice().createCommandPoolUnique(poolInfo);
		}
		catch (const vk::SystemError& err) {
			vfLogError("Failed to create command pool: {}", err.what());
		}
	}

	void CommandPool::cleanUp() {
		for (auto& comd : commandBuffers) {
			comd.reset();
		}
		commandBuffers.clear();
		commandPool.reset();
	}

	void CommandPool::recreate() {
		// Clear old command buffers before allocating new ones
		// (The unique handles will properly free them)
		commandBuffers.clear();
		allocateCommandBuffers();
	}

	void CommandPool::resetCommandBuffer(uint32_t index)
	{
		try {
			commandBuffers[index].get().reset(vk::CommandBufferResetFlagBits::eReleaseResources);  // Reset with resource release
		}
		catch (const vk::SystemError& err) {
			vfLogError("Failed to reset command buffer: {}", err.what());
		}
	}

}
