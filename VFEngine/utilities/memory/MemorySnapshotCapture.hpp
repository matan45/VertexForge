#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "VramAssetSnapshot.hpp"
#include "../cpumem/CpuMemorySnapshot.hpp" // CategoryKind

// VK-1539 — named memory-snapshot capture + diff (leak-hunt workflow).
//
// This header is deliberately Vulkan-free, graphics-free and JSON-free so it is
// reachable from the CPU-only Tests project (the diff is the load-bearing, unit-
// tested logic) and reusable by a future runtime perf HUD. JSON/CSV serialization
// lives editor-side in MemoryDiagnosticsWindow so this header stays dependency-light.

namespace memory {

	// One CPU RAM category as it stood at capture time (mirrors CategoryView, but
	// stored by value into the capture so it survives the live snapshot being refreshed).
	struct CapturedCpuCat {
		std::string name;
		CategoryKind kind = CategoryKind::Other;
		uint64_t bytes = 0;
		uint64_t peak = 0;
	};

	// One GPU heap aggregate (device-local / host-visible / dedicated / staging).
	struct GpuHeapEntry {
		std::string name;
		uint64_t usedBytes = 0;
		uint64_t capacityBytes = 0;
	};

	// A full, named point-in-time memory capture. New axes are additive — bump
	// schemaVersion only on a semantic change to an existing field.
	struct MemorySnapshotCapture {
		uint32_t schemaVersion = 1;
		std::string label;
		int64_t timestampUnixMs = 0;

		// CPU axis
		std::vector<CapturedCpuCat> cpuCategories;
		uint64_t cpuTotalTrackedBytes = 0;
		uint64_t cpuBudgetBytes = 0;

		// GPU heap-aggregate axis (not the per-block list — keeps captures small)
		std::vector<GpuHeapEntry> gpuHeaps;
		uint64_t vramBudgetBytes = 0;
		uint64_t vramUsageBytes = 0;

		// Per-asset VRAM axis
		std::vector<VramAssetRow> vramAssets;
		uint64_t vramTextureTotal = 0;
		uint64_t vramMeshTotal = 0;
		uint64_t vramVtTotal = 0;
	};

	enum class DeltaKind : uint8_t { Added, Removed, Changed, Unchanged };

	inline const char* deltaKindName(DeltaKind k) {
		switch (k) {
		case DeltaKind::Added:     return "Added";
		case DeltaKind::Removed:   return "Removed";
		case DeltaKind::Changed:   return "Changed";
		case DeltaKind::Unchanged: return "Unchanged";
		}
		return "Unknown";
	}

	// One keyed row of a diff. `before`/`after` are the full rows (default-constructed
	// on the absent side for Added/Removed). byteDelta = after.bytes - before.bytes.
	template <class Row>
	struct DiffRow {
		DeltaKind kind = DeltaKind::Unchanged;
		std::string key;
		Row before{};
		Row after{};
		int64_t byteDelta = 0;
	};

	struct MemorySnapshotDiff {
		std::vector<DiffRow<CapturedCpuCat>> cpuDeltas;
		std::vector<DiffRow<GpuHeapEntry>>   heapDeltas;
		std::vector<DiffRow<VramAssetRow>>   assetDeltas; // keyed by asset name → Added/Removed is the leak signal
		int64_t cpuTotalDelta = 0;
		int64_t vramUsageDelta = 0;
	};

	// Keyed set-join over two row vectors. Emits after-order rows first (Added/
	// Changed/Unchanged), then before-only rows (Removed) in before-order — fully
	// deterministic given the input order.
	template <class Row, class KeyFn, class BytesFn>
	inline std::vector<DiffRow<Row>> diffRows(const std::vector<Row>& before,
	                                          const std::vector<Row>& after,
	                                          KeyFn key, BytesFn bytesOf) {
		std::vector<DiffRow<Row>> out;
		out.reserve(after.size() + before.size());

		std::unordered_map<std::string, const Row*> beforeByKey;
		beforeByKey.reserve(before.size());
		for (const Row& b : before)
			beforeByKey.emplace(key(b), &b);

		std::unordered_set<std::string> seen;
		seen.reserve(after.size());

		for (const Row& a : after) {
			std::string k = key(a);
			seen.insert(k);
			DiffRow<Row> d;
			d.after = a;
			auto it = beforeByKey.find(k);
			if (it == beforeByKey.end()) {
				d.kind = DeltaKind::Added;
				d.byteDelta = static_cast<int64_t>(bytesOf(a));
			} else {
				const Row& b = *it->second;
				d.before = b;
				const int64_t delta = static_cast<int64_t>(bytesOf(a)) - static_cast<int64_t>(bytesOf(b));
				d.kind = (delta != 0) ? DeltaKind::Changed : DeltaKind::Unchanged;
				d.byteDelta = delta;
			}
			d.key = std::move(k);
			out.push_back(std::move(d));
		}

		for (const Row& b : before) {
			std::string k = key(b);
			if (seen.find(k) != seen.end())
				continue;
			DiffRow<Row> d;
			d.kind = DeltaKind::Removed;
			d.before = b;
			d.byteDelta = -static_cast<int64_t>(bytesOf(b));
			d.key = std::move(k);
			out.push_back(std::move(d));
		}

		return out;
	}

	inline MemorySnapshotDiff diff(const MemorySnapshotCapture& before,
	                               const MemorySnapshotCapture& after) {
		MemorySnapshotDiff d;
		d.cpuDeltas = diffRows<CapturedCpuCat>(
			before.cpuCategories, after.cpuCategories,
			[](const CapturedCpuCat& r) { return r.name; },
			[](const CapturedCpuCat& r) { return r.bytes; });
		d.heapDeltas = diffRows<GpuHeapEntry>(
			before.gpuHeaps, after.gpuHeaps,
			[](const GpuHeapEntry& r) { return r.name; },
			[](const GpuHeapEntry& r) { return r.usedBytes; });
		d.assetDeltas = diffRows<VramAssetRow>(
			before.vramAssets, after.vramAssets,
			[](const VramAssetRow& r) { return r.name; },
			[](const VramAssetRow& r) { return r.bytes; });
		d.cpuTotalDelta = static_cast<int64_t>(after.cpuTotalTrackedBytes) - static_cast<int64_t>(before.cpuTotalTrackedBytes);
		d.vramUsageDelta = static_cast<int64_t>(after.vramUsageBytes) - static_cast<int64_t>(before.vramUsageBytes);
		return d;
	}

}
