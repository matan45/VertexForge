#pragma once

#include <atomic>
#include <cstdint>

namespace memory {

	struct GpuAllocationStats {
		static inline std::atomic<uint64_t> legacyAllocationCount{0};
		static inline std::atomic<uint64_t> legacyAllocatedBytes{0};
		static inline std::atomic<uint64_t> managedAllocationCount{0};
		static inline std::atomic<uint64_t> managedAllocatedBytes{0};
	};

}
