#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

namespace memory {

	// Coarse owner class for a per-asset VRAM row, shown in the memory profiler's
	// "VRAM Assets" tab and captured into memory snapshots (VK-1539).
	enum class VramAssetCategory : uint8_t {
		Texture,        // streamed mip-resident texture (TextureStreamManager)
		Mesh,           // resident merged-mesh geometry (MergedMeshBuffer)
		VirtualTexture, // resident SVT atlas pages attributed per owning image
		Other
	};

	inline const char* vramAssetCategoryName(VramAssetCategory c) {
		switch (c) {
		case VramAssetCategory::Texture:        return "Texture";
		case VramAssetCategory::Mesh:           return "Mesh";
		case VramAssetCategory::VirtualTexture: return "Virtual Texture";
		case VramAssetCategory::Other:          return "Other";
		}
		return "Unknown";
	}

	// One attributed asset: its owner name (asset path) + resident VRAM bytes.
	struct VramAssetRow {
		std::string name;
		VramAssetCategory category = VramAssetCategory::Other;
		uint64_t bytes = 0;
	};

	// Per-asset VRAM attribution assembled on the render thread by GPUDrivenRenderer
	// (it owns TextureStreamManager / MergedMeshBuffer / SVTManager) and consumed by
	// the editor MemoryDiagnosticsWindow — which cannot include graphics/core. Published
	// behind a mutex on the same static-storage model as GpuMemorySnapshot, so no
	// Services round-trip is needed and the live containers are only ever walked on the
	// thread that owns them.
	//
	// textureTotalBytes is the exact running total that also feeds
	// CullingDebugStats.textureStream.vramUsedBytes, so the tab reconciles with the
	// Culling Stats window by construction. mesh/vt totals reconcile internally only.
	struct VramAssetSnapshot {
		std::vector<VramAssetRow> rows;      // pre-sorted by bytes desc, truncated to top-N
		uint64_t textureTotalBytes = 0;
		uint64_t meshTotalBytes = 0;
		uint64_t vtTotalBytes = 0;
		uint64_t generation = 0;             // bumped on each publish; lets the reader skip work

		static std::mutex& mutex() {
			static std::mutex m;
			return m;
		}

		// Producer side (render thread): publish a freshly built attribution.
		static void publish(VramAssetSnapshot&& snap) {
			std::lock_guard<std::mutex> lock(mutex());
			storage() = std::move(snap);
		}

		// Consumer side (UI thread): take a copy for this frame's draw.
		static VramAssetSnapshot read() {
			std::lock_guard<std::mutex> lock(mutex());
			return storage();
		}

	private:
		static VramAssetSnapshot& storage() {
			static VramAssetSnapshot snap;
			return snap;
		}
	};

}
