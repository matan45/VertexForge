#include "BufferUtilities.hpp"
#include "VulkanMemoryManager.hpp"
#include "VulkanContext.hpp"
#include "Utilities.hpp"
#include "memory/GpuAllocationStats.hpp"
#include <cstring>

namespace core
{
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

		auto* dev = VulkanContext::getDeviceRaw();
		auto& memManager = dev->getMemoryManager();

		// Create staging buffer with host-visible memory
		vk::Buffer stagingBuffer;
		VulkanAllocation stagingAllocation;

		BufferInfoRequest stagingInfo(
			device,
			physicalDevice,
			size,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);
		createBuffer(stagingInfo, stagingBuffer, stagingAllocation, memManager);

		std::memcpy(stagingAllocation.mappedPtr, srcData, static_cast<size_t>(size));

		// Copy from staging to destination buffer with offset
		try {
			auto cmd = Utilities::beginSingleTimeCommands(device, commandPool);
			vk::BufferCopy copyRegion{ 0, offset, size };
			cmd->copyBuffer(stagingBuffer, dstBuffer, copyRegion);
			Utilities::endSingleTimeCommands(queue, cmd);
		} catch (...) {
			BufferUtilities::destroyBuffer(device, stagingBuffer, stagingAllocation, memManager);
			throw;
		}

		// Cleanup staging buffer
		BufferUtilities::destroyBuffer(device, stagingBuffer, stagingAllocation, memManager);
	}

}
