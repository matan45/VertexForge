#pragma once

#include "VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <mutex>

namespace core
{
	class Device;

	struct StagingRegion
	{
		vk::DeviceSize offset = 0;
		void* mappedPtr = nullptr;
		vk::DeviceSize size = 0;
		bool isOverflow = false;

		// Overflow fallback fields (only valid when isOverflow == true)
		vk::Buffer overflowBuffer;
		VulkanAllocation overflowAllocation;
	};

	class StagingRingBuffer
	{
	public:
		StagingRingBuffer(Device& device, vk::DeviceSize ringSize = 0);
		~StagingRingBuffer();

		StagingRingBuffer(const StagingRingBuffer&) = delete;
		StagingRingBuffer& operator=(const StagingRingBuffer&) = delete;

		StagingRegion allocate(vk::DeviceSize size);

		// Advance the read offset when a transfer completes (called by TransferManager)
		void advanceReadOffset(vk::DeviceSize newReadOffset);

		vk::Buffer getBuffer() const { return buffer; }
		vk::DeviceSize getRingSize() const { return ringSize; }
		vk::DeviceSize getAvailableSpace() const { std::lock_guard lock(ringMutex); return availableSpace(); }
		static inline std::atomic<uint32_t> overflowCount{0};

		void cleanupOverflow(StagingRegion& region);
		void updateGlobalStats() const;

	private:
		Device& ownerDevice;
		const vk::Device& device;

		vk::Buffer buffer;
		VulkanAllocation bufferAllocation;
		void* baseMappedPtr = nullptr;
		vk::DeviceSize ringSize;

		vk::DeviceSize writeOffset = 0;
		vk::DeviceSize readOffset = 0;
		vk::DeviceSize copyAlignment = 1;

		mutable std::mutex ringMutex;

		vk::DeviceSize availableSpace() const;
	};
}
