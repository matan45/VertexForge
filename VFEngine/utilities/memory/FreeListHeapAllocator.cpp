#include "FreeListHeapAllocator.hpp"
#include <algorithm>
#include <limits>

namespace memory {

	std::atomic<uint32_t> FreeListHeapAllocator::nextAllocatorId{1};

	FreeListHeapAllocator::FreeListHeapAllocator(uint64_t capacity, const std::string& name)
		: capacity(capacity)
		, name(name)
		, allocatorId(nextAllocatorId.fetch_add(1)) {
		buffer = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(capacity)));

		// Start with one free block spanning the entire heap
		freeBlocks.push_back({0, capacity});
	}

	FreeListHeapAllocator::~FreeListHeapAllocator() {
		std::free(buffer);
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

		FreeBlock& block = freeBlocks[bestIndex];
		uint64_t alignedOffset = alignUp(block.offset, alignment);
		uint64_t alignmentWaste = alignedOffset - block.offset;
		uint64_t totalRequired = alignmentWaste + size;

		AllocationHandle handle;
		handle.offset = alignedOffset;
		handle.size = size;
		handle.allocatorId = allocatorId;

		// If alignment created a gap at the start, keep it as a free block
		if (alignmentWaste > 0) {
			uint64_t remainingAfter = block.size - totalRequired;
			// Shrink original block to the alignment gap
			block.size = alignmentWaste;

			// If there's space after the allocation, add a new free block
			if (remainingAfter > 0) {
				freeBlocks.push_back({alignedOffset + size, remainingAfter});
			}
		} else {
			uint64_t remaining = block.size - size;
			if (remaining > 0) {
				block.offset += size;
				block.size = remaining;
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

		freeBlocks.push_back({handle.offset, handle.size});
		totalAllocated -= handle.size;
		freeCount++;

		coalesce();
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

	void FreeListHeapAllocator::coalesce() {
		if (freeBlocks.size() < 2) {
			return;
		}

		std::sort(freeBlocks.begin(), freeBlocks.end(),
			[](const FreeBlock& a, const FreeBlock& b) { return a.offset < b.offset; });

		std::vector<FreeBlock> merged;
		merged.reserve(freeBlocks.size());
		merged.push_back(freeBlocks[0]);

		for (size_t i = 1; i < freeBlocks.size(); ++i) {
			FreeBlock& last = merged.back();
			const FreeBlock& current = freeBlocks[i];

			if (last.offset + last.size == current.offset) {
				last.size += current.size;
			} else {
				merged.push_back(current);
			}
		}

		freeBlocks = std::move(merged);
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
