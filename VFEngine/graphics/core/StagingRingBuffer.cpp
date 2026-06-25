#include "StagingRingBuffer.hpp"
#include "BufferUtilities.hpp"
#include "Device.hpp"
#include "memory/GpuAllocationStats.hpp"
#include "print/Log.hpp"
#include "cpumem/CpuMemoryManager.hpp"
#include "cpumem/CpuMemoryCategories.hpp"

namespace core
{
	static constexpr vk::DeviceSize FALLBACK_RING_SIZE = 64ull * 1024 * 1024;

	// Resolve both staging category ids exactly once per process lifetime.
	// Function-local statics are initialised the first time updateGlobalStats or
	// allocate (overflow path) is reached — well after CpuMemoryManager::instance()
	// is live.  Both are lock-free after the one-time init (the init itself is
	// thread-safe under C++11 magic-static rules).
	static memory::CategoryId stagingRingCategoryId()
	{
		static const memory::CategoryId id =
			memory::CpuMemoryManager::instance().registerCategory(
				memory::categories::UploadStagingRing, memory::CategoryKind::Staging);
		return id;
	}

	static memory::CategoryId stagingOverflowCategoryId()
	{
		static const memory::CategoryId id =
			memory::CpuMemoryManager::instance().registerCategory(
				memory::categories::UploadStagingOverflow, memory::CategoryKind::Staging);
		return id;
	}

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

		// Query optimal copy alignment
		auto props = device.getPhysicalDevice().getProperties();
		copyAlignment = std::max(static_cast<vk::DeviceSize>(1), props.limits.optimalBufferCopyOffsetAlignment);

		vfLogInfo("StagingRingBuffer: Created {}MB ring buffer", this->ringSize / (1024 * 1024));
	}

	StagingRingBuffer::~StagingRingBuffer()
	{
		// TransferManager::waitAll() ensures all fences are waited on before this destructor runs
		baseMappedPtr = nullptr;
		auto& memManager = ownerDevice.getMemoryManager();
		BufferUtilities::destroyBuffer(device, buffer, bufferAllocation, memManager);
	}

	StagingRegion StagingRingBuffer::allocate(vk::DeviceSize size)
	{
		std::lock_guard lock(ringMutex);

		// Align writeOffset to optimalBufferCopyOffsetAlignment
		vk::DeviceSize alignedWrite = (writeOffset + copyAlignment - 1) & ~(copyAlignment - 1);

		// Check if we have enough contiguous space
		vk::DeviceSize available = availableSpace();

		// Try linear allocation (no wrap)
		vk::DeviceSize alignedSize = (alignedWrite - writeOffset) + size;
		if (alignedWrite + size <= ringSize && alignedSize <= available) {
			StagingRegion region;
			region.offset = alignedWrite;
			region.mappedPtr = static_cast<uint8_t*>(baseMappedPtr) + alignedWrite;
			region.size = size;
			region.isOverflow = false;
			writeOffset = alignedWrite + size;
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

		// Account for the overflow buffer in the CPU memory manager.  Store the
		// size on the region so cleanupOverflow can subtract the exact amount even
		// after region.size could theoretically be zeroed by a reset.
		region.overflowAccountedBytes = static_cast<uint64_t>(size);
		memory::CpuMemoryManager::instance().addUsage(stagingOverflowCategoryId(), region.overflowAccountedBytes);

		return region;
	}

	void StagingRingBuffer::advanceReadOffset(vk::DeviceSize newReadOffset)
	{
		std::lock_guard lock(ringMutex);
		readOffset = newReadOffset;
	}

	void StagingRingBuffer::cleanupOverflow(StagingRegion& region)
	{
		if (!region.isOverflow) {
			return;
		}
		auto& memManager = ownerDevice.getMemoryManager();
		BufferUtilities::destroyBuffer(device, region.overflowBuffer, region.overflowAllocation, memManager);

		// Read the accounted size before the region is zeroed, then subtract.
		// This is the only subUsage site for overflow — one add in allocate, one
		// sub here, so the accounting is exactly balanced.
		if (region.overflowAccountedBytes > 0) {
			memory::CpuMemoryManager::instance().subUsage(stagingOverflowCategoryId(), region.overflowAccountedBytes);
		}

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
		// stagingPendingTransfers is owned/published by TransferManager (it tracks the
		// in-flight operations), so it is intentionally not written here.
		memory::GpuAllocationStats::stagingOverflowCount.store(overflowCount.load(std::memory_order_relaxed), std::memory_order_relaxed);

		// Publish ring usage to the CPU memory manager (lock-free atomic write —
		// setUsage does NOT take any CpuMemory mutex, so holding ringMutex here is
		// safe; there is no lock-order edge introduced).
		memory::CpuMemoryManager::instance().setUsage(stagingRingCategoryId(), static_cast<uint64_t>(used));
	}
}
