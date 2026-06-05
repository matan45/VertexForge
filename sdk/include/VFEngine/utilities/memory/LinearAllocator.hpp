#pragma once

#include "IAllocator.hpp"
#include <atomic>
#include <cstdlib>

namespace memory {

	class LinearAllocator : public IAllocator {
	public:
		LinearAllocator(uint64_t capacity, const std::string& name = "LinearAllocator");
		~LinearAllocator() override;

		LinearAllocator(const LinearAllocator&) = delete;
		LinearAllocator& operator=(const LinearAllocator&) = delete;

		AllocationHandle allocate(uint64_t size, uint64_t alignment = 1) override;
		void free(const AllocationHandle& handle) override;
		void reset() override;
		AllocatorStats getStats() const override;

		const std::string& getName() const override { return name; }
		AllocatorStrategy getStrategy() const override { return AllocatorStrategy::Linear; }

		void* getPointer(const AllocationHandle& handle) const;
		uint64_t getCapacity() const { return capacity; }

	private:
		uint8_t* buffer = nullptr;
		uint64_t capacity = 0;
		std::atomic<uint64_t> currentOffset{0};
		std::atomic<uint64_t> allocationCount{0};
		uint64_t peakAllocated = 0;
		std::string name;
		uint32_t allocatorId;

		static std::atomic<uint32_t> nextAllocatorId;

		static uint64_t alignUp(uint64_t value, uint64_t alignment) {
			return (value + alignment - 1) & ~(alignment - 1);
		}
	};

}
