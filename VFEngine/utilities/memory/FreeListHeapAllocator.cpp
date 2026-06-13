#include "FreeListHeapAllocator.hpp"
#include <algorithm>
#include <limits>

namespace memory {

	std::atomic<uint32_t> FreeListHeapAllocator::nextAllocatorId{1};

	FreeListHeapAllocator::FreeListHeapAllocator(uint64_t capacity, const std::string& name,
		bool backingBuffer)
		: capacity(capacity)
		, name(name)
		, allocatorId(nextAllocatorId.fetch_add(1)) {
		if (backingBuffer) {
			buffer = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(capacity)));
		}

		// Start with one free block spanning the entire heap
		freeBlocks.push_back({0, capacity});
	}

	FreeListHeapAllocator::~FreeListHeapAllocator() {
		std::free(buffer); // free(nullptr) is safe
	}

	AllocationHandle FreeListHeapAllocator::allocate(uint64_t size, uint64_t alignment) {
		if (size == 0) {
			return {};
		}

		std::lock_guard<std::mutex> lock(mtx);

		size_t bestIndex = findBestFit(size, alignment);
		if (bestIndex == std::numeric_limits<size_t>::max()) {
			return {};
		}

		// Capture by value: inserting into freeBlocks below may reallocate and
		// invalidate a reference into the vector.
		const uint64_t blockOffset = freeBlocks[bestIndex].offset;
		const uint64_t blockSize = freeBlocks[bestIndex].size;
		uint64_t alignedOffset = alignUp(blockOffset, alignment);
		uint64_t alignmentWaste = alignedOffset - blockOffset;
		uint64_t totalRequired = alignmentWaste + size;

		AllocationHandle handle;
		handle.offset = alignedOffset;
		handle.size = size;
		handle.allocatorId = allocatorId;

		// If alignment created a gap at the start, keep it as a free block. The
		// gap keeps the same offset (sorted order preserved) and the trailing
		// remainder lands immediately after it (index bestIndex + 1).
		if (alignmentWaste > 0) {
			uint64_t remainingAfter = blockSize - totalRequired;
			freeBlocks[bestIndex].size = alignmentWaste;
			if (remainingAfter > 0) {
				freeBlocks.insert(freeBlocks.begin() + bestIndex + 1,
					{alignedOffset + size, remainingAfter});
			}
		} else {
			uint64_t remaining = blockSize - size;
			if (remaining > 0) {
				// Shrink in place; new offset stays below the next block's offset.
				freeBlocks[bestIndex].offset += size;
				freeBlocks[bestIndex].size = remaining;
			} else {
				freeBlocks.erase(freeBlocks.begin() + bestIndex);
			}
		}

		totalAllocated += size;
		allocationCount++;
		if (totalAllocated > peakAllocated) {
			peakAllocated = totalAllocated;
		}

		return handle;
	}

	void FreeListHeapAllocator::free(const AllocationHandle& handle) {
		if (!handle.isValid()) {
			return;
		}

		std::lock_guard<std::mutex> lock(mtx);

		insertFreeBlock(handle.offset, handle.size);
		totalAllocated -= handle.size;
		freeCount++;
	}

	void FreeListHeapAllocator::reset() {
		std::lock_guard<std::mutex> lock(mtx);

		freeBlocks.clear();
		freeBlocks.push_back({0, capacity});
		totalAllocated = 0;
		allocationCount = 0;
		freeCount = 0;
	}

	AllocatorStats FreeListHeapAllocator::getStats() const {
		std::lock_guard<std::mutex> lock(mtx);

		AllocatorStats stats;
		stats.totalAllocated = totalAllocated;
		stats.peakAllocated = peakAllocated;
		stats.allocationCount = allocationCount;
		stats.freeCount = freeCount;
		stats.activeAllocationCount = allocationCount - freeCount;
		stats.totalCapacity = capacity;

		uint64_t totalFree = 0;
		uint64_t largestFree = 0;
		for (const auto& block : freeBlocks) {
			totalFree += block.size;
			if (block.size > largestFree) {
				largestFree = block.size;
			}
		}

		stats.largestFreeBlock = largestFree;
		if (totalFree > 0 && freeBlocks.size() > 1) {
			stats.fragmentationPercent = (1.0f - static_cast<float>(largestFree) / static_cast<float>(totalFree)) * 100.0f;
		} else {
			stats.fragmentationPercent = 0.0f;
		}

		return stats;
	}

	void* FreeListHeapAllocator::getPointer(const AllocationHandle& handle) const {
		if (!handle.isValid() || handle.offset + handle.size > capacity) {
			return nullptr;
		}
		return buffer + handle.offset;
	}

	uint64_t FreeListHeapAllocator::getFreeBytes() const {
		std::lock_guard<std::mutex> lock(mtx);
		return capacity - totalAllocated;
	}

	std::vector<FreeSpan> FreeListHeapAllocator::getFreeSpans() const {
		std::lock_guard<std::mutex> lock(mtx);
		std::vector<FreeSpan> spans;
		spans.reserve(freeBlocks.size());
		for (const auto& block : freeBlocks) {
			spans.push_back({block.offset, block.size});
		}
		return spans;
	}

	void FreeListHeapAllocator::insertFreeBlock(uint64_t offset, uint64_t size) {
		// freeBlocks is kept sorted by offset and fully coalesced, so the freed
		// region can only be adjacent to its immediate neighbours. Find the first
		// block that starts at/after the freed offset (binary search).
		size_t pos = static_cast<size_t>(
			std::lower_bound(freeBlocks.begin(), freeBlocks.end(), offset,
				[](const FreeBlock& b, uint64_t off) { return b.offset < off; })
			- freeBlocks.begin());

		// Merge with the left neighbour if it is address-contiguous.
		bool mergedLeft = false;
		if (pos > 0) {
			FreeBlock& left = freeBlocks[pos - 1];
			if (left.offset + left.size == offset) {
				left.size += size;
				offset = left.offset;
				size = left.size;
				mergedLeft = true;
			}
		}

		// Merge with the right neighbour if the (possibly left-merged) region is
		// address-contiguous with it.
		if (pos < freeBlocks.size() && offset + size == freeBlocks[pos].offset) {
			if (mergedLeft) {
				freeBlocks[pos - 1].size += freeBlocks[pos].size;
				freeBlocks.erase(freeBlocks.begin() + pos);
			} else {
				freeBlocks[pos].offset = offset;
				freeBlocks[pos].size = size + freeBlocks[pos].size;
			}
			return;
		}

		if (!mergedLeft) {
			freeBlocks.insert(freeBlocks.begin() + pos, {offset, size});
		}
	}

	size_t FreeListHeapAllocator::findBestFit(uint64_t requiredSize, uint64_t alignment) const {
		size_t bestIndex = std::numeric_limits<size_t>::max();
		uint64_t bestFitSize = std::numeric_limits<uint64_t>::max();

		for (size_t i = 0; i < freeBlocks.size(); ++i) {
			const FreeBlock& block = freeBlocks[i];
			uint64_t alignedOffset = alignUp(block.offset, alignment);
			uint64_t alignmentWaste = alignedOffset - block.offset;
			uint64_t totalRequired = alignmentWaste + requiredSize;

			if (block.size >= totalRequired && block.size < bestFitSize) {
				bestFitSize = block.size;
				bestIndex = i;

				// Exact fit, no need to search further
				if (block.size == totalRequired) {
					break;
				}
			}
		}

		return bestIndex;
	}

}
