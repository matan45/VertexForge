#include "LinearAllocator.hpp"
#include <algorithm>

namespace memory {

	std::atomic<uint32_t> LinearAllocator::nextAllocatorId{1};

	LinearAllocator::LinearAllocator(uint64_t capacity, const std::string& name)
		: capacity(capacity)
		, name(name)
		, allocatorId(nextAllocatorId.fetch_add(1)) {
		buffer = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(capacity)));
	}

	LinearAllocator::~LinearAllocator() {
		std::free(buffer);
	}

	AllocationHandle LinearAllocator::allocate(uint64_t size, uint64_t alignment) {
		if (size == 0) {
			return {};
		}

		uint64_t currentPos = currentOffset.load(std::memory_order_relaxed);
		uint64_t alignedPos;
		uint64_t newPos;

		do {
			alignedPos = alignUp(currentPos, alignment);
			newPos = alignedPos + size;

			if (newPos > capacity) {
				return {};
			}
		} while (!currentOffset.compare_exchange_weak(currentPos, newPos,
			std::memory_order_release, std::memory_order_relaxed));

		allocationCount.fetch_add(1, std::memory_order_relaxed);

		// Update peak (relaxed, approximate is fine for stats)
		uint64_t currentPeak = peakAllocated;
		while (newPos > currentPeak) {
			peakAllocated = newPos;
			currentPeak = newPos;
		}

		AllocationHandle handle;
		handle.offset = alignedPos;
		handle.size = size;
		handle.allocatorId = allocatorId;
		return handle;
	}

	void LinearAllocator::free(const AllocationHandle& /*handle*/) {
		// Linear allocator does not support individual frees
		// Memory is reclaimed only via reset()
	}

	void LinearAllocator::reset() {
		currentOffset.store(0, std::memory_order_release);
		allocationCount.store(0, std::memory_order_relaxed);
	}

	AllocatorStats LinearAllocator::getStats() const {
		AllocatorStats stats;
		stats.totalAllocated = currentOffset.load(std::memory_order_relaxed);
		stats.peakAllocated = peakAllocated;
		stats.allocationCount = allocationCount.load(std::memory_order_relaxed);
		stats.freeCount = 0;
		stats.totalCapacity = capacity;
		stats.largestFreeBlock = capacity - stats.totalAllocated;
		stats.fragmentationPercent = 0.0f; // Linear allocators have zero fragmentation
		return stats;
	}

	void* LinearAllocator::getPointer(const AllocationHandle& handle) const {
		if (!handle.isValid() || handle.offset + handle.size > capacity) {
			return nullptr;
		}
		return buffer + handle.offset;
	}

}
