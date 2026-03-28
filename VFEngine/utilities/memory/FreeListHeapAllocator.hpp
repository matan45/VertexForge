#pragma once

#include "IAllocator.hpp"
#include <vector>
#include <mutex>
#include <cstdlib>

namespace memory {

	class FreeListHeapAllocator : public IAllocator {
	public:
		// When backingBuffer is false, no CPU memory is allocated (offset-only tracking for GPU blocks)
		FreeListHeapAllocator(uint64_t capacity, const std::string& name = "FreeListHeapAllocator",
			bool backingBuffer = false);
		~FreeListHeapAllocator() override;

		FreeListHeapAllocator(const FreeListHeapAllocator&) = delete;
		FreeListHeapAllocator& operator=(const FreeListHeapAllocator&) = delete;

		AllocationHandle allocate(uint64_t size, uint64_t alignment = 1) override;
		void free(const AllocationHandle& handle) override;
		void reset() override;
		AllocatorStats getStats() const override;

		const std::string& getName() const override { return name; }
		AllocatorStrategy getStrategy() const override { return AllocatorStrategy::FreeList; }

		void* getPointer(const AllocationHandle& handle) const;
		uint64_t getCapacity() const { return capacity; }

	private:
		struct FreeBlock {
			uint64_t offset;
			uint64_t size;
		};

		uint8_t* buffer = nullptr;
		uint64_t capacity = 0;
		std::vector<FreeBlock> freeBlocks;
		mutable std::mutex mtx;
		uint64_t totalAllocated = 0;
		uint64_t peakAllocated = 0;
		uint64_t allocationCount = 0;
		uint64_t freeCount = 0;
		std::string name;
		uint32_t allocatorId;

		static std::atomic<uint32_t> nextAllocatorId;

		static uint64_t alignUp(uint64_t value, uint64_t alignment) {
			return (value + alignment - 1) & ~(alignment - 1);
		}

		void coalesce();
		size_t findBestFit(uint64_t requiredSize, uint64_t alignment) const;
	};

}
