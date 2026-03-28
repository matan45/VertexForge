#include "MemoryDiagnostics.hpp"
#include "IAllocator.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace memory {

	MemoryDiagnostics& MemoryDiagnostics::instance()
	{
		static MemoryDiagnostics diag;
		return diag;
	}

	void MemoryDiagnostics::registerAllocator(IAllocator* allocator)
	{
		if (!allocator) return;

		std::lock_guard lock(diagMutex);
		// Avoid duplicates
		for (const auto& reg : allocators) {
			if (reg.allocator == allocator) return;
		}

		RegisteredAllocator reg;
		reg.allocator = allocator;
		reg.name = allocator->getName();
		reg.strategy = allocator->getStrategy();
		allocators.push_back(reg);
	}

	void MemoryDiagnostics::unregisterAllocator(IAllocator* allocator)
	{
		if (!allocator) return;

		std::lock_guard lock(diagMutex);
		allocators.erase(
			std::remove_if(allocators.begin(), allocators.end(),
				[allocator](const RegisteredAllocator& reg) { return reg.allocator == allocator; }),
			allocators.end());
	}

	AllocatorStats MemoryDiagnostics::getGlobalStats() const
	{
		std::lock_guard lock(diagMutex);

		AllocatorStats global;
		for (const auto& reg : allocators) {
			auto stats = reg.allocator->getStats();
			global.totalAllocated += stats.totalAllocated;
			global.peakAllocated += stats.peakAllocated;
			global.allocationCount += stats.allocationCount;
			global.freeCount += stats.freeCount;
			global.totalCapacity += stats.totalCapacity;
			global.largestFreeBlock = std::max(global.largestFreeBlock, stats.largestFreeBlock);
		}

		if (global.totalCapacity > 0 && global.totalAllocated < global.totalCapacity) {
			uint64_t totalFree = global.totalCapacity - global.totalAllocated;
			if (totalFree > 0 && global.largestFreeBlock < totalFree) {
				global.fragmentationPercent = (1.0f - static_cast<float>(global.largestFreeBlock) / static_cast<float>(totalFree)) * 100.0f;
			}
		}

		return global;
	}

	AllocatorStats MemoryDiagnostics::getAllocatorStats(const std::string& name) const
	{
		std::lock_guard lock(diagMutex);

		for (const auto& reg : allocators) {
			if (reg.name == name) {
				return reg.allocator->getStats();
			}
		}

		return {};
	}

	std::vector<std::pair<std::string, AllocatorStats>> MemoryDiagnostics::getAllStats() const
	{
		std::lock_guard lock(diagMutex);

		std::vector<std::pair<std::string, AllocatorStats>> result;
		result.reserve(allocators.size());
		for (const auto& reg : allocators) {
			result.emplace_back(reg.name, reg.allocator->getStats());
		}
		return result;
	}

	void MemoryDiagnostics::logSummary() const
	{
		std::lock_guard lock(diagMutex);

		vfLogInfo("=== Memory Diagnostics Summary ===");
		vfLogInfo("Registered allocators: {}", allocators.size());

		uint64_t globalAllocated = 0;
		uint64_t globalCapacity = 0;

		for (const auto& reg : allocators) {
			auto stats = reg.allocator->getStats();
			globalAllocated += stats.totalAllocated;
			globalCapacity += stats.totalCapacity;

			const char* strategyName = "Unknown";
			switch (reg.strategy) {
				case AllocatorStrategy::Linear: strategyName = "Linear"; break;
				case AllocatorStrategy::Pool: strategyName = "Pool"; break;
				case AllocatorStrategy::FreeList: strategyName = "FreeList"; break;
			}

			vfLogInfo("  [{}] {} | Used: {}KB / {}KB | Allocs: {} | Frees: {} | Frag: {:.1f}%",
				strategyName,
				reg.name,
				stats.totalAllocated / 1024,
				stats.totalCapacity / 1024,
				stats.allocationCount,
				stats.freeCount,
				stats.fragmentationPercent);
		}

		vfLogInfo("Total CPU: {}KB / {}KB",
			globalAllocated / 1024, globalCapacity / 1024);

		// GPU stats if callback is set
		if (gpuStatsCallback) {
			auto gpuInfo = gpuStatsCallback();
			if (!gpuInfo.empty()) {
				vfLogInfo("{}", gpuInfo);
			}
		}

		vfLogInfo("=================================");
	}

	void MemoryDiagnostics::setGpuStatsCallback(GpuStatsCallback callback)
	{
		std::lock_guard lock(diagMutex);
		gpuStatsCallback = std::move(callback);
	}

}
