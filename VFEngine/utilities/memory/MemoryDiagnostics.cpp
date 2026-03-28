#include "MemoryDiagnostics.hpp"
#include "GpuAllocationStats.hpp"
#include "print/Log.hpp"

namespace memory {

	MemoryDiagnostics& MemoryDiagnostics::instance()
	{
		static MemoryDiagnostics diag;
		return diag;
	}

	void MemoryDiagnostics::logSummary() const
	{
		vfLogInfo("=== Memory Diagnostics Summary ===");

		uint64_t managedCount = GpuAllocationStats::managedAllocationCount.load(std::memory_order_relaxed);
		uint64_t managedBytes = GpuAllocationStats::managedAllocatedBytes.load(std::memory_order_relaxed);
		vfLogInfo("Managed allocations: {} ({:.1f}MB)", managedCount,
			static_cast<float>(managedBytes) / (1024.0f * 1024.0f));

		uint32_t dlBlocks = GpuAllocationStats::deviceLocalBlockCount.load(std::memory_order_relaxed);
		uint64_t dlUsed = GpuAllocationStats::deviceLocalUsedBytes.load(std::memory_order_relaxed);
		uint64_t dlCap = GpuAllocationStats::deviceLocalCapacityBytes.load(std::memory_order_relaxed);
		uint32_t dlFrag = GpuAllocationStats::deviceLocalFragPercent.load(std::memory_order_relaxed);
		vfLogInfo("Device Local: {} blocks, {:.1f}MB / {:.1f}MB, frag {:.1f}%",
			dlBlocks,
			static_cast<float>(dlUsed) / (1024.0f * 1024.0f),
			static_cast<float>(dlCap) / (1024.0f * 1024.0f),
			static_cast<float>(dlFrag) / 100.0f);

		uint32_t hvBlocks = GpuAllocationStats::hostVisibleBlockCount.load(std::memory_order_relaxed);
		uint64_t hvUsed = GpuAllocationStats::hostVisibleUsedBytes.load(std::memory_order_relaxed);
		uint64_t hvCap = GpuAllocationStats::hostVisibleCapacityBytes.load(std::memory_order_relaxed);
		uint32_t hvFrag = GpuAllocationStats::hostVisibleFragPercent.load(std::memory_order_relaxed);
		vfLogInfo("Host Visible: {} blocks, {:.1f}MB / {:.1f}MB, frag {:.1f}%",
			hvBlocks,
			static_cast<float>(hvUsed) / (1024.0f * 1024.0f),
			static_cast<float>(hvCap) / (1024.0f * 1024.0f),
			static_cast<float>(hvFrag) / 100.0f);

		uint32_t dedCount = GpuAllocationStats::dedicatedAllocationCount.load(std::memory_order_relaxed);
		uint64_t dedBytes = GpuAllocationStats::dedicatedAllocatedBytes.load(std::memory_order_relaxed);
		vfLogInfo("Dedicated: {} allocations, {:.1f}MB", dedCount,
			static_cast<float>(dedBytes) / (1024.0f * 1024.0f));

		uint64_t reclaimed = GpuAllocationStats::blocksReclaimed.load(std::memory_order_relaxed);
		if (reclaimed > 0) {
			vfLogInfo("Blocks reclaimed: {}", reclaimed);
		}

		vfLogInfo("=================================");
	}

}
