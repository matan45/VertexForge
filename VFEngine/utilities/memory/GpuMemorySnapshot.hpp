#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include "MemoryTypes.hpp"

namespace memory {

	// Per-block occupancy view for the diagnostics fragmentation map.
	struct GpuBlockView {
		uint32_t memoryTypeIndex = 0;
		bool hostVisible = false;
		bool deviceAddress = false;
		uint64_t capacity = 0;
		uint64_t used = 0;
		float fragmentationPercent = 0.0f;
		std::vector<FreeSpan> freeSpans; // address-ordered free regions
	};

	// One oversized/device-address dedicated allocation.
	struct DedicatedView {
		uint64_t size = 0;
		bool hostVisible = false;
		bool deviceAddress = false;
	};

	// Heavy diagnostic snapshot assembled by VulkanMemoryManager (graphics) and
	// consumed by the editor MemoryDiagnosticsWindow (which cannot include
	// graphics/core). Published behind a mutex on the same static-storage model
	// as GpuAllocationStats, so no Services round-trip is needed.
	struct GpuMemorySnapshot {
		std::vector<GpuBlockView> blocks;
		std::vector<DedicatedView> dedicated;

		static std::mutex& mutex() {
			static std::mutex m;
			return m;
		}

		// Producer side: publish a freshly built snapshot.
		static void publish(GpuMemorySnapshot&& snap) {
			std::lock_guard<std::mutex> lock(mutex());
			storage() = std::move(snap);
		}

		// Consumer side: take a copy for this frame's draw.
		static GpuMemorySnapshot read() {
			std::lock_guard<std::mutex> lock(mutex());
			return storage();
		}

	private:
		static GpuMemorySnapshot& storage() {
			static GpuMemorySnapshot snap;
			return snap;
		}
	};

}
