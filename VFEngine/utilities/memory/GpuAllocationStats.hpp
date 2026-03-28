#pragma once

#include <atomic>
#include <cstdint>

namespace memory {

	// Runtime allocation counters (updated by BufferUtilities/ImageUtilities)
	struct GpuAllocationStats {
		static inline std::atomic<uint64_t> managedAllocationCount{0};
		static inline std::atomic<uint64_t> managedAllocatedBytes{0};
	};

	// Configurable memory pool sizes (read at engine startup, editable from UI for next launch)
	struct MemoryPoolConfig {
		uint64_t deviceLocalBlockSizeMB = 256;
		uint64_t hostVisibleBlockSizeMB = 64;
		uint64_t stagingRingBufferSizeMB = 64;

		uint64_t deviceLocalBlockSize() const { return deviceLocalBlockSizeMB * 1024 * 1024; }
		uint64_t hostVisibleBlockSize() const { return hostVisibleBlockSizeMB * 1024 * 1024; }
		uint64_t stagingRingBufferSize() const { return stagingRingBufferSizeMB * 1024 * 1024; }

		// Singleton - loaded once at startup, editable from UI (applies on restart)
		static MemoryPoolConfig& instance() {
			static MemoryPoolConfig config;
			return config;
		}

		bool dirty = false; // True if UI changed values (needs restart to apply)
	};

}
