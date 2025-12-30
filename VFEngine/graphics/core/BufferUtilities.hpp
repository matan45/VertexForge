#pragma once

#include <vulkan/vulkan.hpp>

namespace core
{
	struct BufferInfoRequest
	{
		const vk::Device& logicalDevice;
		const vk::PhysicalDevice& physicalDevice;
		vk::DeviceSize size;
		vk::BufferUsageFlags usage;
		vk::MemoryPropertyFlags properties;

		explicit BufferInfoRequest(
			const vk::Device& logicalDevice,
			const vk::PhysicalDevice& physicalDevice,
			vk::DeviceSize size = 1024,
			vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal
		)
			: logicalDevice{ logicalDevice },
			physicalDevice{ physicalDevice },
			size{ size },
			usage{ usage },
			properties{ properties }
		{
		}
	};

	class BufferUtilities
	{
	private:
		BufferUtilities() = delete;
		~BufferUtilities() = delete;

	public:
		static void createBuffer(const BufferInfoRequest& bufferInfo, vk::Buffer& buffer,
			vk::DeviceMemory& bufferMemory);

		static void copyToBuffer(
			const vk::Device& device,
			const vk::PhysicalDevice& physicalDevice,
			const vk::Queue& queue,
			const vk::CommandPool& commandPool,
			vk::Buffer dstBuffer,
			const void* srcData,
			vk::DeviceSize size,
			vk::DeviceSize offset = 0
		);

		static void destroyBuffer(const vk::Device& device, vk::Buffer& buffer, vk::DeviceMemory& memory);
	};
}
