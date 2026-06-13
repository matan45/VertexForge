#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace memory {

	static constexpr uint64_t INVALID_ALLOCATION = UINT64_MAX;

	enum class AllocatorStrategy : uint8_t {
		Linear,
		Pool,
		FreeList
	};

	struct AllocationHandle {
		uint64_t offset = INVALID_ALLOCATION;
		uint64_t size = 0;
		uint32_t allocatorId = 0;

		bool isValid() const { return offset != INVALID_ALLOCATION; }

		bool operator==(const AllocationHandle& other) const {
			return offset == other.offset && size == other.size && allocatorId == other.allocatorId;
		}

		bool operator!=(const AllocationHandle& other) const {
			return !(*this == other);
		}
	};

	struct AllocationInfo {
		void* ptr = nullptr;
		uint64_t offset = 0;
		uint64_t size = 0;
		uint64_t alignment = 0;
	};

	struct HeapConfig {
		uint64_t heapSize = 0;
		AllocatorStrategy strategy = AllocatorStrategy::FreeList;
		std::string name;
	};

	struct AllocatorStats {
		uint64_t totalAllocated = 0;
		uint64_t peakAllocated = 0;
		uint64_t allocationCount = 0;
		uint64_t freeCount = 0;
		uint64_t activeAllocationCount = 0;
		uint64_t largestFreeBlock = 0;
		uint64_t totalCapacity = 0;
		float fragmentationPercent = 0.0f;
	};

	// A free region within an allocator's backing range. Used to build the
	// per-block fragmentation/occupancy map in the memory-diagnostics window.
	struct FreeSpan {
		uint64_t offset = 0;
		uint64_t size = 0;
	};

}
