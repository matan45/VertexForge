#include "DebugAllocatorWrapper.hpp"
#include "IAllocator.hpp"
#include "print/Log.hpp"

namespace memory {

	DebugAllocatorTracker& DebugAllocatorTracker::instance()
	{
		static DebugAllocatorTracker tracker;
		return tracker;
	}

	void DebugAllocatorTracker::trackAllocation(uint32_t allocatorId, const AllocationHandle& handle,
		uint64_t size, uint64_t alignment, const char* file, int line)
	{
		std::lock_guard lock(trackerMutex);

		AllocationKey key{allocatorId, handle.offset};

		DebugAllocationRecord record;
		record.size = size;
		record.alignment = alignment;
		record.file = file;
		record.line = line;
		record.timestamp = nextTimestamp++;

		activeAllocations[key] = record;

		auto& stats = allocatorStats[allocatorId];
		stats.totalAllocations++;
		stats.currentAllocated += size;
		if (stats.currentAllocated > stats.peakAllocated) {
			stats.peakAllocated = stats.currentAllocated;
		}
	}

	void DebugAllocatorTracker::trackFree(uint32_t allocatorId, const AllocationHandle& handle,
		const char* /*file*/, int /*line*/)
	{
		std::lock_guard lock(trackerMutex);

		AllocationKey key{allocatorId, handle.offset};
		auto it = activeAllocations.find(key);
		if (it != activeAllocations.end()) {
			auto& stats = allocatorStats[allocatorId];
			stats.totalFrees++;
			stats.currentAllocated -= it->second.size;
			activeAllocations.erase(it);
		}
	}

	void DebugAllocatorTracker::reportLeaks() const
	{
		std::lock_guard lock(trackerMutex);

		if (activeAllocations.empty()) {
			vfLogInfo("DebugAllocatorTracker: No memory leaks detected");
			return;
		}

		vfLogWarning("DebugAllocatorTracker: {} potential memory leak(s) detected:", activeAllocations.size());

		// Group by allocator
		std::unordered_map<uint32_t, std::vector<std::pair<AllocationKey, DebugAllocationRecord>>> grouped;
		for (const auto& [key, record] : activeAllocations) {
			grouped[key.allocatorId].push_back({key, record});
		}

		for (const auto& [allocatorId, leaks] : grouped) {
			uint64_t totalLeaked = 0;
			for (const auto& [key, record] : leaks) {
				totalLeaked += record.size;
			}

			vfLogWarning("  Allocator {}: {} leak(s), {} bytes total", allocatorId, leaks.size(), totalLeaked);

			// Report up to 10 leaks per allocator
			size_t reported = 0;
			for (const auto& [key, record] : leaks) {
				if (reported >= 10) {
					vfLogWarning("    ... and {} more", leaks.size() - 10);
					break;
				}
				vfLogWarning("    Leak: {} bytes at offset {}, allocated at {}:{}",
					record.size, key.offset,
					record.file ? record.file : "<unknown>", record.line);
				reported++;
			}
		}
	}

	void DebugAllocatorTracker::reportLeaksForAllocator(uint32_t allocatorId, const std::string& name) const
	{
		std::lock_guard lock(trackerMutex);

		uint64_t leakCount = 0;
		uint64_t leakBytes = 0;
		for (const auto& [key, record] : activeAllocations) {
			if (key.allocatorId == allocatorId) {
				leakCount++;
				leakBytes += record.size;
			}
		}

		if (leakCount == 0) {
			return;
		}

		vfLogWarning("DebugAllocatorTracker: Allocator '{}' has {} leak(s), {} bytes", name, leakCount, leakBytes);

		size_t reported = 0;
		for (const auto& [key, record] : activeAllocations) {
			if (key.allocatorId != allocatorId) continue;
			if (reported >= 10) {
				vfLogWarning("  ... and {} more", leakCount - 10);
				break;
			}
			vfLogWarning("  Leak: {} bytes at offset {}, allocated at {}:{}",
				record.size, key.offset,
				record.file ? record.file : "<unknown>", record.line);
			reported++;
		}
	}

	void DebugAllocatorTracker::clear()
	{
		std::lock_guard lock(trackerMutex);
		activeAllocations.clear();
		allocatorStats.clear();
	}

	DebugAllocatorTracker::AllocatorSummary DebugAllocatorTracker::getSummary(uint32_t allocatorId) const
	{
		std::lock_guard lock(trackerMutex);

		AllocatorSummary summary;
		auto it = allocatorStats.find(allocatorId);
		if (it != allocatorStats.end()) {
			summary.name = it->second.name;
			summary.totalAllocations = it->second.totalAllocations;
			summary.totalFrees = it->second.totalFrees;
			summary.peakAllocated = it->second.peakAllocated;
			summary.currentAllocated = it->second.currentAllocated;
		}

		// Count active leaks
		for (const auto& [key, record] : activeAllocations) {
			if (key.allocatorId == allocatorId) {
				summary.leakCount++;
			}
		}

		return summary;
	}

#ifdef DEBUG
	AllocationHandle debugAllocate(IAllocator& allocator, uint64_t size, uint64_t alignment,
		const char* file, int line)
	{
		auto handle = allocator.allocate(size, alignment);
		if (handle.isValid()) {
			DebugAllocatorTracker::instance().trackAllocation(
				handle.allocatorId, handle, size, alignment, file, line);
		}
		return handle;
	}

	void debugFree(IAllocator& allocator, const AllocationHandle& handle,
		const char* file, int line)
	{
		if (handle.isValid()) {
			DebugAllocatorTracker::instance().trackFree(
				handle.allocatorId, handle, file, line);
		}
		allocator.free(handle);
	}
#endif

}
