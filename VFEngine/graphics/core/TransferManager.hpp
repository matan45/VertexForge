#pragma once

#include <vulkan/vulkan.hpp>
#include <mutex>
#include <vector>
#include <memory>
#include "StagingRingBuffer.hpp"

namespace core
{
	// Async transfer operation tracking
	struct TransferOperation {
		vk::Fence fence;
		vk::CommandBuffer commandBuffer;
		bool completed = false;

		// Only set for overflow staging (ring buffer couldn't fit)
		StagingRegion overflowRegion;

		// End offset in the ring buffer for fence tracking
		vk::DeviceSize ringEndOffset = 0;
	};

	// TransferManager: Handles async buffer/image transfers using fences
	// Uses dedicated transfer queue if available, falls back to graphics queue
	class Device;

	class TransferManager {
	private:
		Device& ownerDevice;
		const vk::Device& device;
		const vk::PhysicalDevice& physicalDevice;
		uint32_t transferQueueFamily;

		vk::CommandPool commandPool;
		mutable std::mutex transferMutex;
		std::vector<TransferOperation> pendingTransfers;

		std::unique_ptr<StagingRingBuffer> ringBuffer;

	public:
		TransferManager(Device& ownerDevice, uint32_t transferQueueFamily);
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
