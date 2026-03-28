#pragma once

#include <atomic>
#include <cstdint>

namespace memory {

	// Runtime allocation counters (updated by BufferUtilities/ImageUtilities/VulkanMemoryManager)
	struct GpuAllocationStats {
		// Total managed allocations (createBuffer/createImage through VulkanMemoryManager)
		static inline std::atomic<uint64_t> managedAllocationCount{0};
		static inline std::atomic<uint64_t> managedAllocatedBytes{0};

		// Per-type block stats (updated by VulkanMemoryManager)
		static inline std::atomic<uint32_t> deviceLocalBlockCount{0};
		static inline std::atomic<uint64_t> deviceLocalUsedBytes{0};
		static inline std::atomic<uint64_t> deviceLocalCapacityBytes{0};
		static inline std::atomic<uint32_t> deviceLocalFragPercent{0}; // x100 for precision

		static inline std::atomic<uint32_t> hostVisibleBlockCount{0};
		static inline std::atomic<uint64_t> hostVisibleUsedBytes{0};
		static inline std::atomic<uint64_t> hostVisibleCapacityBytes{0};
		static inline std::atomic<uint32_t> hostVisibleFragPercent{0};

		// Dedicated allocations (oversized, device-address)
		static inline std::atomic<uint32_t> dedicatedAllocationCount{0};
		static inline std::atomic<uint64_t> dedicatedAllocatedBytes{0};

		// Block reclamation
		static inline std::atomic<uint64_t> blocksReclaimed{0};

		// Staging ring buffer stats
		static inline std::atomic<uint64_t> stagingRingSize{0};
		static inline std::atomic<uint64_t> stagingRingUsed{0};
		static inline std::atomic<uint32_t> stagingPendingTransfers{0};
		static inline std::atomic<uint32_t> stagingOverflowCount{0};
	};

	// Configurable memory pool sizes (read at engine startup, editable from UI for next launch)
	struct MemoryPoolConfig {
		uint64_t deviceLocalBlockSizeMB = 256;
		uint64_t hostVisibleBlockSizeMB = 64;
		uint64_t stagingRingBufferSizeMB = 64;
		uint64_t dedicatedThresholdMB = 64;

		uint64_t deviceLocalBlockSize() const { return deviceLocalBlockSizeMB * 1024 * 1024; }
		uint64_t hostVisibleBlockSize() const { return hostVisibleBlockSizeMB * 1024 * 1024; }
		uint64_t stagingRingBufferSize() const { return stagingRingBufferSizeMB * 1024 * 1024; }
		uint64_t dedicatedThreshold() const { return dedicatedThresholdMB * 1024 * 1024; }

		// Singleton - loaded once at startup, editable from UI (applies on restart)
		static MemoryPoolConfig& instance() {
			static MemoryPoolConfig config;
			return config;
		}

		bool dirty = false; // True if UI changed values (needs restart to apply)
	};

}
