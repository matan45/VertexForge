#include "StagingRingBuffer.hpp"
#include "BufferUtilities.hpp"
#include "Device.hpp"
#include "memory/GpuAllocationStats.hpp"
#include "print/Log.hpp"

namespace core
{
	static constexpr vk::DeviceSize FALLBACK_RING_SIZE = 64ull * 1024 * 1024;

	StagingRingBuffer::StagingRingBuffer(Device& device, vk::DeviceSize ringSize)
		: ownerDevice(device)
		, device(device.getLogicalDevice())
		, ringSize(ringSize > 0 ? ringSize : FALLBACK_RING_SIZE)
	{
		auto& memManager = device.getMemoryManager();

		// Create a single large host-visible staging buffer
		BufferInfoRequest bufferInfo(
			this->device,
			device.getPhysicalDevice(),
			this->ringSize,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);
		BufferUtilities::createBuffer(bufferInfo, buffer, bufferAllocation, memManager);

		// Persistently map
		baseMappedPtr = bufferAllocation.mappedPtr;

		vfLogInfo("StagingRingBuffer: Created {}MB ring buffer", this->ringSize / (1024 * 1024));
	}

	StagingRingBuffer::~StagingRingBuffer()
	{
		// Wait for all pending fences
		if (!pendingFences.empty()) {
			std::vector<vk::Fence> fences;
			fences.reserve(pendingFences.size());
			for (const auto& marker : pendingFences) {
				fences.push_back(marker.fence);
			}
			(void)device.waitForFences(fences, VK_TRUE, UINT64_MAX);
			pendingFences.clear();
		}

		baseMappedPtr = nullptr;
		auto& memManager = ownerDevice.getMemoryManager();
		BufferUtilities::destroyBuffer(device, buffer, bufferAllocation, memManager);
	}

	StagingRegion StagingRingBuffer::allocate(vk::DeviceSize size)
	{
		std::lock_guard lock(ringMutex);

		// Try to poll completed fences first
		auto it = pendingFences.begin();
		while (it != pendingFences.end()) {
			if (device.getFenceStatus(it->fence) == vk::Result::eSuccess) {
				readOffset = it->endOffset;
				it = pendingFences.erase(it);
			} else {
				break; // Fences are ordered, stop at first incomplete
			}
		}

		// Check if we have enough contiguous space
		vk::DeviceSize available = availableSpace();

		// Try linear allocation (no wrap)
		if (writeOffset + size <= ringSize && size <= available) {
			StagingRegion region;
			region.offset = writeOffset;
			region.mappedPtr = static_cast<uint8_t*>(baseMappedPtr) + writeOffset;
			region.size = size;
			region.isOverflow = false;
			writeOffset += size;
			return region;
		}

		// Try wrapping to beginning if there's space there
		if (writeOffset >= readOffset && readOffset >= size) {
			// Waste the remaining space at the end, wrap to 0
			writeOffset = size;
			StagingRegion region;
			region.offset = 0;
			region.mappedPtr = baseMappedPtr;
			region.size = size;
			region.isOverflow = false;
			return region;
		}

		// If request is larger than ring buffer or no space, use overflow
		vfLogWarning("StagingRingBuffer: Overflow allocation for {}KB", size / 1024);
		overflowCount.fetch_add(1, std::memory_order_relaxed);

		StagingRegion region;
		region.isOverflow = true;
		region.size = size;

		auto& memManager = ownerDevice.getMemoryManager();
		BufferInfoRequest overflowInfo(
			device,
			ownerDevice.getPhysicalDevice(),
			size,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);
		BufferUtilities::createBuffer(overflowInfo, region.overflowBuffer, region.overflowAllocation, memManager);
		region.mappedPtr = region.overflowAllocation.mappedPtr;
		region.offset = 0;

		return region;
	}

	void StagingRingBuffer::markFence(vk::Fence fence, vk::DeviceSize endOffset)
	{
		std::lock_guard lock(ringMutex);
		pendingFences.push_back({fence, endOffset});
	}

	void StagingRingBuffer::pollFences()
	{
		std::lock_guard lock(ringMutex);

		auto it = pendingFences.begin();
		while (it != pendingFences.end()) {
			if (device.getFenceStatus(it->fence) == vk::Result::eSuccess) {
				readOffset = it->endOffset;
				it = pendingFences.erase(it);
			} else {
				break;
			}
		}
	}

	void StagingRingBuffer::cleanupOverflow(StagingRegion& region)
	{
		if (!region.isOverflow) {
			return;
		}
		auto& memManager = ownerDevice.getMemoryManager();
		BufferUtilities::destroyBuffer(device, region.overflowBuffer, region.overflowAllocation, memManager);
		region = {};
	}

	vk::DeviceSize StagingRingBuffer::availableSpace() const
	{
		if (writeOffset >= readOffset) {
			// Free space is: (ringSize - writeOffset) + readOffset
			return (ringSize - writeOffset) + readOffset;
		}
		// readOffset > writeOffset: free space between them
		return readOffset - writeOffset;
	}

	void StagingRingBuffer::updateGlobalStats() const
	{
		std::lock_guard lock(ringMutex);
		vk::DeviceSize used = (writeOffset >= readOffset) ? (writeOffset - readOffset) : (ringSize - readOffset + writeOffset);
		memory::GpuAllocationStats::stagingRingSize.store(ringSize, std::memory_order_relaxed);
		memory::GpuAllocationStats::stagingRingUsed.store(used, std::memory_order_relaxed);
		memory::GpuAllocationStats::stagingPendingTransfers.store(static_cast<uint32_t>(pendingFences.size()), std::memory_order_relaxed);
		memory::GpuAllocationStats::stagingOverflowCount.store(overflowCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
	}
}
