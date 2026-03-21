#include "TransferManager.hpp"
#include "BufferUtilities.hpp"
#include <cstring>

namespace core {

	TransferManager::TransferManager(const vk::Device& device, const vk::PhysicalDevice& physicalDevice,
	                                 const vk::Queue& transferQueue, uint32_t transferQueueFamily)
		: device(device), physicalDevice(physicalDevice), transferQueue(transferQueue),
		  transferQueueFamily(transferQueueFamily)
	{
		vk::CommandPoolCreateInfo poolInfo{};
		poolInfo.queueFamilyIndex = transferQueueFamily;
		poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
		                 vk::CommandPoolCreateFlagBits::eTransient;
		commandPool = device.createCommandPool(poolInfo);
	}

	TransferManager::~TransferManager()
	{
		waitAll();
		if (commandPool) {
			device.destroyCommandPool(commandPool);
		}
	}

	void TransferManager::copyToBufferAsync(vk::Buffer dstBuffer, const void* srcData,
	                                        vk::DeviceSize size, vk::DeviceSize offset)
	{
		if (size == 0 || srcData == nullptr) {
			return;
		}

		// Poll first to clean up any completed transfers
		pollTransfers();

		std::lock_guard lock(transferMutex);

		TransferOperation op{};

		// Create fence for this transfer
		vk::FenceCreateInfo fenceInfo{};
		op.fence = device.createFence(fenceInfo);

		// Create staging buffer
		BufferInfoRequest stagingInfo(device, physicalDevice, size,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		BufferUtilities::createBuffer(stagingInfo, op.stagingBuffer, op.stagingMemory);

		void* mappedData = device.mapMemory(op.stagingMemory, 0, size, {});
		std::memcpy(mappedData, srcData, static_cast<size_t>(size));
		device.unmapMemory(op.stagingMemory);

		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = commandPool;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;
		auto cmdBuffers = device.allocateCommandBuffers(allocInfo);
		op.commandBuffer = cmdBuffers[0];

		// Record copy command
		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		op.commandBuffer.begin(beginInfo);
		vk::BufferCopy copyRegion{0, offset, size};
		op.commandBuffer.copyBuffer(op.stagingBuffer, dstBuffer, copyRegion);
		op.commandBuffer.end();

		// Submit to transfer queue with fence
		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &op.commandBuffer;
		transferQueue.submit(submitInfo, op.fence);

		pendingTransfers.push_back(op);
	}

	void TransferManager::pollTransfers()
	{
		std::lock_guard lock(transferMutex);

		auto it = pendingTransfers.begin();
		while (it != pendingTransfers.end()) {
			// Check if transfer is complete (non-blocking)
			vk::Result result = device.getFenceStatus(it->fence);
			if (result == vk::Result::eSuccess) {
				cleanupTransfer(*it);
				it = pendingTransfers.erase(it);
			} else {
				++it;
			}
		}
	}

	void TransferManager::waitAll()
	{
		std::vector<TransferOperation> localPending;
		{
			std::lock_guard lock(transferMutex);
			localPending = std::move(pendingTransfers);
			pendingTransfers.clear();
		}

		if (localPending.empty()) return;

		std::vector<vk::Fence> fences;
		fences.reserve(localPending.size());
		for (const auto& op : localPending)
			fences.push_back(op.fence);

		if (!fences.empty())
			(void)device.waitForFences(fences, VK_TRUE, UINT64_MAX);

		for (auto& op : localPending)
			cleanupTransfer(op);
	}

	void TransferManager::cleanupTransfer(TransferOperation& op)
	{
		if (op.fence) {
			device.destroyFence(op.fence);
		}
		if (op.commandBuffer) {
			device.freeCommandBuffers(commandPool, op.commandBuffer);
		}
		if (op.stagingBuffer) {
			device.destroyBuffer(op.stagingBuffer);
		}
		if (op.stagingMemory) {
			device.freeMemory(op.stagingMemory);
		}
	}

}
