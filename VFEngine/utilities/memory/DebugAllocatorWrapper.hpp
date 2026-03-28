#pragma once

#include "MemoryTypes.hpp"
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace memory {

	class IAllocator;

	struct DebugAllocationRecord {
		uint64_t size = 0;
		uint64_t alignment = 0;
		const char* file = nullptr;
		int line = 0;
		uint64_t timestamp = 0;
	};

	class DebugAllocatorTracker {
	public:
		static DebugAllocatorTracker& instance();

		void trackAllocation(uint32_t allocatorId, const AllocationHandle& handle,
			uint64_t size, uint64_t alignment, const char* file, int line);
		void trackFree(uint32_t allocatorId, const AllocationHandle& handle,
			const char* file, int line);

		void reportLeaks() const;
		void reportLeaksForAllocator(uint32_t allocatorId, const std::string& name) const;
		void clear();

		struct AllocatorSummary {
			std::string name;
			uint64_t totalAllocations = 0;
			uint64_t totalFrees = 0;
			uint64_t peakAllocated = 0;
			uint64_t currentAllocated = 0;
			uint64_t leakCount = 0;
		};

		AllocatorSummary getSummary(uint32_t allocatorId) const;

	private:
		DebugAllocatorTracker() = default;

		struct AllocationKey {
			uint32_t allocatorId;
			uint64_t offset;

			bool operator==(const AllocationKey& other) const {
				return allocatorId == other.allocatorId && offset == other.offset;
			}
		};

		struct AllocationKeyHash {
			size_t operator()(const AllocationKey& key) const {
				return std::hash<uint64_t>()(key.offset) ^ (std::hash<uint32_t>()(key.allocatorId) << 32);
			}
		};

		struct AllocatorStats {
			std::string name;
			uint64_t totalAllocations = 0;
			uint64_t totalFrees = 0;
			uint64_t peakAllocated = 0;
			uint64_t currentAllocated = 0;
		};

		mutable std::mutex trackerMutex;
		std::unordered_map<AllocationKey, DebugAllocationRecord, AllocationKeyHash> activeAllocations;
		std::unordered_map<uint32_t, AllocatorStats> allocatorStats;
		uint64_t nextTimestamp = 0;
	};

}
