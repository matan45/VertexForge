#include "BufferUtilities.hpp"
#include "MemoryUtilities.hpp"
#include "VulkanMemoryManager.hpp"
#include "VulkanContext.hpp"
#include "Utilities.hpp"
#include "memory/GpuAllocationStats.hpp"
#include <cstring>

namespace {
	core::VulkanMemoryManager* getGlobalMemManager()
	{
		auto* dev = core::VulkanContext::getDeviceRaw();
		if (dev) {
			return &dev->getMemoryManager();
		}
		return nullptr;
	}
}

namespace core
{
	void BufferUtilities::createBuffer(const BufferInfoRequest& bufferInfo, vk::Buffer& buffer, vk::DeviceMemory& bufferMemory)
	{
		if (bufferInfo.size == 0) return;

		vk::BufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.size = bufferInfo.size;
		bufferCreateInfo.usage = bufferInfo.usage;
		bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

		buffer = bufferInfo.logicalDevice.createBuffer(bufferCreateInfo);

		vk::MemoryRequirements memRequirements = bufferInfo.logicalDevice.getBufferMemoryRequirements(buffer);
		bool needsDeviceAddress = (bufferInfo.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) != vk::BufferUsageFlags{};

		// Route through VulkanMemoryManager if available
		auto* memManager = getGlobalMemManager();
		if (memManager) {
			auto allocation = memManager->allocateLegacy(memRequirements, bufferInfo.properties, needsDeviceAddress);
			bufferMemory = allocation.memory;
			bufferInfo.logicalDevice.bindBufferMemory(buffer, bufferMemory, 0);

			memory::GpuAllocationStats::managedAllocationCount.fetch_add(1, std::memory_order_relaxed);
			memory::GpuAllocationStats::managedAllocatedBytes.fetch_add(memRequirements.size, std::memory_order_relaxed);
			return;
		}

		// Fallback: raw allocation (during early startup before Device is ready)
		vk::MemoryAllocateFlagsInfo allocFlags{};
		if (needsDeviceAddress) {
			allocFlags.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
		}

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.pNext = needsDeviceAddress ? &allocFlags : nullptr;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = MemoryUtilities::findMemoryType(
			bufferInfo.physicalDevice, memRequirements.memoryTypeBits, bufferInfo.properties);

		bufferMemory = bufferInfo.logicalDevice.allocateMemory(allocInfo);
		bufferInfo.logicalDevice.bindBufferMemory(buffer, bufferMemory, 0);

		memory::GpuAllocationStats::legacyAllocationCount.fetch_add(1, std::memory_order_relaxed);
		memory::GpuAllocationStats::legacyAllocatedBytes.fetch_add(memRequirements.size, std::memory_order_relaxed);
	}

	void BufferUtilities::createBuffer(const BufferInfoRequest& bufferInfo, vk::Buffer& buffer,
		VulkanAllocation& allocation, VulkanMemoryManager& memManager)
	{
		if (bufferInfo.size == 0) return;

		vk::BufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.size = bufferInfo.size;
		bufferCreateInfo.usage = bufferInfo.usage;
		bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

		buffer = bufferInfo.logicalDevice.createBuffer(bufferCreateInfo);

		vk::MemoryRequirements memRequirements = bufferInfo.logicalDevice.getBufferMemoryRequirements(buffer);

		bool needsDeviceAddress = (bufferInfo.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) != vk::BufferUsageFlags{};

		allocation = memManager.allocate(memRequirements, bufferInfo.properties, needsDeviceAddress);
		bufferInfo.logicalDevice.bindBufferMemory(buffer, allocation.memory, allocation.offset);

		memory::GpuAllocationStats::managedAllocationCount.fetch_add(1, std::memory_order_relaxed);
		memory::GpuAllocationStats::managedAllocatedBytes.fetch_add(memRequirements.size, std::memory_order_relaxed);
	}

	void BufferUtilities::destroyBuffer(const vk::Device& device, vk::Buffer& buffer,
		VulkanAllocation& allocation, VulkanMemoryManager& memManager)
	{
		if (buffer) {
			device.destroyBuffer(buffer);
			buffer = nullptr;
		}
		if (allocation.isValid()) {
			memManager.free(allocation);
			allocation = {};
		}
	}

	void BufferUtilities::copyToBuffer(
		const vk::Device& device,
		const vk::PhysicalDevice& physicalDevice,
		const vk::Queue& queue,
		const vk::CommandPool& commandPool,
		vk::Buffer dstBuffer,
		const void* srcData,
		vk::DeviceSize size,
		vk::DeviceSize offset)
	{
		if (size == 0 || srcData == nullptr) {
			return;
		}

		// Create staging buffer with host-visible memory
		vk::Buffer stagingBuffer;
		vk::DeviceMemory stagingMemory;

		BufferInfoRequest stagingInfo(
			device,
			physicalDevice,
			size,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);
		createBuffer(stagingInfo, stagingBuffer, stagingMemory);

		void* mappedData = device.mapMemory(stagingMemory, 0, size, {});
		std::memcpy(mappedData, srcData, static_cast<size_t>(size));
		device.unmapMemory(stagingMemory);

		// Copy from staging to destination buffer with offset
		try {
			auto cmd = Utilities::beginSingleTimeCommands(device, commandPool);
			vk::BufferCopy copyRegion{ 0, offset, size };
			cmd->copyBuffer(stagingBuffer, dstBuffer, copyRegion);
			Utilities::endSingleTimeCommands(queue, cmd);
		} catch (...) {
			device.destroyBuffer(stagingBuffer);
			device.freeMemory(stagingMemory);
			throw;
		}

		// Cleanup staging buffer
		device.destroyBuffer(stagingBuffer);
		device.freeMemory(stagingMemory);
	}

	void BufferUtilities::destroyBuffer(const vk::Device& device, vk::Buffer& buffer, vk::DeviceMemory& memory)
	{
		if (buffer) {
			device.destroyBuffer(buffer);
			buffer = nullptr;
		}
		if (memory) {
			auto* memManager = getGlobalMemManager();
			if (memManager) {
				memManager->freeLegacy(memory);
			} else {
				device.freeMemory(memory);
			}
			memory = nullptr;
		}
	}
}
