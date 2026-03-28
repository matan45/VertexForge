#include "TransferManager.hpp"
#include "Device.hpp"
#include "BufferUtilities.hpp"
#include <cstring>

namespace core {

	TransferManager::TransferManager(Device& ownerDevice, uint32_t transferQueueFamily)
		: ownerDevice(ownerDevice),
		  device(ownerDevice.getLogicalDevice()),
		  physicalDevice(ownerDevice.getPhysicalDevice()),
		  transferQueueFamily(transferQueueFamily)
	{
		vk::CommandPoolCreateInfo poolInfo{};
		poolInfo.queueFamilyIndex = transferQueueFamily;
		poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
		                 vk::CommandPoolCreateFlagBits::eTransient;
		commandPool = device.createCommandPool(poolInfo);

		ringBuffer = std::make_unique<StagingRingBuffer>(ownerDevice);
	}

	TransferManager::~TransferManager()
	{
		waitAll();
		ringBuffer.reset();
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

		pollTransfers();

		std::lock_guard lock(transferMutex);

		TransferOperation op{};

		vk::FenceCreateInfo fenceInfo{};
		op.fence = device.createFence(fenceInfo);

		// Use ring buffer for staging instead of per-transfer allocation
		auto staging = ringBuffer->allocate(size);
		std::memcpy(staging.mappedPtr, srcData, static_cast<size_t>(size));

		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = commandPool;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;
		auto cmdBuffers = device.allocateCommandBuffers(allocInfo);
		op.commandBuffer = cmdBuffers[0];

		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		op.commandBuffer.begin(beginInfo);

		// Use either ring buffer or overflow buffer as source
		vk::Buffer srcBuffer = staging.isOverflow ? staging.overflowBuffer : ringBuffer->getBuffer();
		vk::BufferCopy copyRegion{staging.offset, offset, size};
		op.commandBuffer.copyBuffer(srcBuffer, dstBuffer, copyRegion);
		op.commandBuffer.end();

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &op.commandBuffer;
		ownerDevice.submitTransfer(submitInfo, op.fence);

		if (staging.isOverflow) {
			op.overflowRegion = staging;
		} else {
			op.ringEndOffset = staging.offset + size;
			ringBuffer->markFence(op.fence, op.ringEndOffset);
		}

		pendingTransfers.push_back(op);
	}

	void TransferManager::pollTransfers()
	{
		std::lock_guard lock(transferMutex);

		ringBuffer->pollFences();
		ringBuffer->updateGlobalStats();

		auto it = pendingTransfers.begin();
		while (it != pendingTransfers.end()) {
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

		if (ringBuffer) {
			ringBuffer->pollFences();
		}
	}

	void TransferManager::cleanupTransfer(TransferOperation& op)
	{
		if (op.fence) {
			device.destroyFence(op.fence);
		}
		if (op.commandBuffer) {
			device.freeCommandBuffers(commandPool, op.commandBuffer);
		}
		// Clean up overflow staging if used
		if (op.overflowRegion.isOverflow) {
			ringBuffer->cleanupOverflow(op.overflowRegion);
		}
	}

}
