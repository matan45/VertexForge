#pragma once

#include "MemoryTypes.hpp"

namespace memory {

	class IAllocator {
	public:
		virtual ~IAllocator() = default;

		virtual AllocationHandle allocate(uint64_t size, uint64_t alignment = 1) = 0;
		virtual void free(const AllocationHandle& handle) = 0;
		virtual void reset() = 0;
		virtual AllocatorStats getStats() const = 0;

		virtual const std::string& getName() const = 0;
		virtual AllocatorStrategy getStrategy() const = 0;
	};

}
