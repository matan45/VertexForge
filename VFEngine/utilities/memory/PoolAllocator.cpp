#include "PoolAllocator.hpp"
#include <algorithm>

namespace memory {

	std::atomic<uint32_t> PoolAllocator::nextAllocatorId{1};

	PoolAllocator::PoolAllocator(uint64_t blockSize, uint32_t blockCount, const std::string& name)
		: blockSize(blockSize)
		, blockCount(blockCount)
		, name(name)
		, allocatorId(nextAllocatorId.fetch_add(1)) {
		buffer = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(blockSize * blockCount)));

		// Initialize free list with all block indices (LIFO order)
		freeList.resize(blockCount);
		for (uint32_t i = 0; i < blockCount; ++i) {
			freeList[i] = blockCount - 1 - i;
		}
	}

	PoolAllocator::~PoolAllocator() {
		std::free(buffer);
	}

	AllocationHandle PoolAllocator::allocate(uint64_t size, uint64_t /*alignment*/) {
		if (size == 0 || size > blockSize) {
			return {};
		}

		std::lock_guard<std::mutex> lock(mtx);

		if (freeList.empty()) {
			return {};
		}

		uint32_t blockIndex = freeList.back();
		freeList.pop_back();
		usedBlocks++;
		totalAllocations++;

		uint64_t currentAllocated = static_cast<uint64_t>(usedBlocks) * blockSize;
		if (currentAllocated > peakAllocated) {
			peakAllocated = currentAllocated;
		}

		AllocationHandle handle;
		handle.offset = static_cast<uint64_t>(blockIndex) * blockSize;
		handle.size = blockSize;
		handle.allocatorId = allocatorId;
		return handle;
	}

	void PoolAllocator::free(const AllocationHandle& handle) {
		if (!handle.isValid()) {
			return;
		}

		std::lock_guard<std::mutex> lock(mtx);

		uint32_t blockIndex = static_cast<uint32_t>(handle.offset / blockSize);
		freeList.push_back(blockIndex);
		usedBlocks--;
		totalFrees++;
	}

	void PoolAllocator::reset() {
		std::lock_guard<std::mutex> lock(mtx);

		freeList.resize(blockCount);
		for (uint32_t i = 0; i < blockCount; ++i) {
			freeList[i] = blockCount - 1 - i;
		}
		usedBlocks = 0;
	}

	AllocatorStats PoolAllocator::getStats() const {
		std::lock_guard<std::mutex> lock(mtx);

		AllocatorStats stats;
		stats.totalAllocated = static_cast<uint64_t>(usedBlocks) * blockSize;
		stats.peakAllocated = peakAllocated;
		stats.allocationCount = totalAllocations;
		stats.freeCount = totalFrees;
		stats.totalCapacity = static_cast<uint64_t>(blockCount) * blockSize;
		stats.largestFreeBlock = freeList.empty() ? 0 : blockSize;
		stats.fragmentationPercent = 0.0f; // Pool allocators have zero fragmentation by design
		return stats;
	}

	void* PoolAllocator::getPointer(const AllocationHandle& handle) const {
		if (!handle.isValid() || handle.offset + blockSize > static_cast<uint64_t>(blockCount) * blockSize) {
			return nullptr;
		}
		return buffer + handle.offset;
	}

}
