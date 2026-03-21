#pragma once

#include <vulkan/vulkan.hpp>
#include <mutex>
#include <vector>

namespace core
{
	// Async transfer operation tracking
	struct TransferOperation {
		vk::Fence fence;
		vk::CommandBuffer commandBuffer;
		vk::Buffer stagingBuffer;
		vk::DeviceMemory stagingMemory;
		bool completed = false;
	};

	// TransferManager: Handles async buffer/image transfers using fences
	// Uses dedicated transfer queue if available, falls back to graphics queue
	class TransferManager {
	private:
		const vk::Device& device;
		const vk::PhysicalDevice& physicalDevice;
		const vk::Queue& transferQueue;
		uint32_t transferQueueFamily;

		vk::CommandPool commandPool;
		mutable std::mutex transferMutex;
		std::vector<TransferOperation> pendingTransfers;
	public:
		TransferManager(const vk::Device& device, const vk::PhysicalDevice& physicalDevice,
		                const vk::Queue& transferQueue, uint32_t transferQueueFamily);
		~TransferManager();

		// Non-copyable
		TransferManager(const TransferManager&) = delete;
		TransferManager& operator=(const TransferManager&) = delete;

		// Submit async buffer copy - returns immediately, call pollTransfers to check completion
		void copyToBufferAsync(vk::Buffer dstBuffer, const void* srcData,
		                       vk::DeviceSize size, vk::DeviceSize offset = 0);

		// Poll and clean up completed transfers
		void pollTransfers();

		// Wait for all pending transfers to complete
		void waitAll();

		// Check if there are pending transfers
		bool hasPendingTransfers() const { std::lock_guard lock(transferMutex); return !pendingTransfers.empty(); }

	private:

		void cleanupTransfer(TransferOperation& op);
	};
}
