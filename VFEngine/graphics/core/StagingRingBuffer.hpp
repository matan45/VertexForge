#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <mutex>

namespace core
{
	class Device;
	struct VulkanAllocation;
	class VulkanMemoryManager;

	struct StagingRegion
	{
		vk::DeviceSize offset = 0;
		void* mappedPtr = nullptr;
		vk::DeviceSize size = 0;
		bool isOverflow = false;

		// Overflow fallback fields (only valid when isOverflow == true)
		vk::Buffer overflowBuffer;
		vk::DeviceMemory overflowMemory;
	};

	class StagingRingBuffer
	{
	public:
		static constexpr vk::DeviceSize DEFAULT_RING_SIZE = 64ull * 1024 * 1024; // 64 MB

		StagingRingBuffer(Device& device, vk::DeviceSize ringSize = DEFAULT_RING_SIZE);
		~StagingRingBuffer();

		StagingRingBuffer(const StagingRingBuffer&) = delete;
		StagingRingBuffer& operator=(const StagingRingBuffer&) = delete;

		StagingRegion allocate(vk::DeviceSize size);
		void markFence(vk::Fence fence, vk::DeviceSize endOffset);
		void pollFences();

		vk::Buffer getBuffer() const { return buffer; }
		vk::DeviceSize getRingSize() const { return ringSize; }

		void cleanupOverflow(StagingRegion& region);

	private:
		Device& ownerDevice;
		const vk::Device& device;

		vk::Buffer buffer;
		vk::DeviceMemory bufferMemory;
		void* baseMappedPtr = nullptr;
		vk::DeviceSize ringSize;

		vk::DeviceSize writeOffset = 0;
		vk::DeviceSize readOffset = 0;

		struct FenceMarker
		{
			vk::Fence fence;
			vk::DeviceSize endOffset;
		};
		std::vector<FenceMarker> pendingFences;

		mutable std::mutex ringMutex;

		vk::DeviceSize availableSpace() const;
	};
}
