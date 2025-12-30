#include "BufferUtilities.hpp"
#include "MemoryUtilities.hpp"
#include "Utilities.hpp"
#include <cstring>

namespace core
{
	void BufferUtilities::createBuffer(const BufferInfoRequest& bufferInfo, vk::Buffer& buffer, vk::DeviceMemory& bufferMemory)
	{
		vk::BufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.size = bufferInfo.size;
		bufferCreateInfo.usage = bufferInfo.usage;
		bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

		buffer = bufferInfo.logicalDevice.createBuffer(bufferCreateInfo);

		vk::MemoryRequirements memRequirements = bufferInfo.logicalDevice.getBufferMemoryRequirements(buffer);
		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = MemoryUtilities::findMemoryType(
			bufferInfo.physicalDevice,
			memRequirements.memoryTypeBits,
			bufferInfo.properties
		);

		bufferMemory = bufferInfo.logicalDevice.allocateMemory(allocInfo);
		bufferInfo.logicalDevice.bindBufferMemory(buffer, bufferMemory, 0);
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
			device.freeMemory(memory);
			memory = nullptr;
		}
	}
}
