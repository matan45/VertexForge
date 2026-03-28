#pragma once

#include "IAllocator.hpp"
#include <vector>
#include <mutex>
#include <cstdlib>

namespace memory {

	class PoolAllocator : public IAllocator {
	public:
		PoolAllocator(uint64_t blockSize, uint32_t blockCount, const std::string& name = "PoolAllocator");
		~PoolAllocator() override;

		PoolAllocator(const PoolAllocator&) = delete;
		PoolAllocator& operator=(const PoolAllocator&) = delete;

		AllocationHandle allocate(uint64_t size, uint64_t alignment = 1) override;
		void free(const AllocationHandle& handle) override;
		void reset() override;
		AllocatorStats getStats() const override;

		const std::string& getName() const override { return name; }
		AllocatorStrategy getStrategy() const override { return AllocatorStrategy::Pool; }

		void* getPointer(const AllocationHandle& handle) const;
		uint64_t getBlockSize() const { return blockSize; }
		uint32_t getBlockCount() const { return blockCount; }

	private:
		uint8_t* buffer = nullptr;
		uint64_t blockSize;
		uint32_t blockCount;
		std::vector<uint32_t> freeList;
		uint32_t usedBlocks = 0;
		uint64_t peakAllocated = 0;
		uint64_t totalAllocations = 0;
		uint64_t totalFrees = 0;
		mutable std::mutex mtx;
		std::string name;
		uint32_t allocatorId;

		static std::atomic<uint32_t> nextAllocatorId;
	};

}
