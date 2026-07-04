#pragma once

// VK-1471 (VFX Slice B3) — per-emitter draw-order key.
//
// An additive integer `sortOrder` on the emitter controls the submission order of
// its indirect draw within the VFX pass. Emitters are stable-sorted by sortOrder
// ascending (lower = submitted first = drawn behind, painter's order); ties keep
// the caller's existing order. Default 0 leaves an effect's draw order unchanged.
//
// Sorting is per-emitter (<=256 draws), never per-particle. This header is pure /
// CPU-testable — no render/GPU types, no Vulkan — so both the scene renderer and the
// unit tests include it. Matches the UICanvasComponent.sortOrder convention used in
// UIFrameBuilderScreenSpace ("Stable sort preserves registry order for ties").

#include <algorithm>
#include <cstdint>
#include <vector>

namespace vfx
{
    // Draw-order descriptor for one emitter's indirect draw / instance group.
    // `index` is the caller's opaque identifier (emitter slot, or a position into a
    // side vector) that is carried through the sort so the caller can reorder its draws.
    struct VFXDrawOrderEntry
    {
        uint32_t index = 0;    // emitter slot / instance index to reorder
        int32_t sortOrder = 0; // ascending submission key (lower is drawn behind)
    };

    // Strict-weak ordering by sortOrder alone; stability provides the tie-break.
    inline bool drawOrderLess(const VFXDrawOrderEntry& a, const VFXDrawOrderEntry& b)
    {
        return a.sortOrder < b.sortOrder;
    }

    // Stable-sort by sortOrder ascending. Equal sortOrder keeps input order, so a
    // uniform sortOrder (e.g. all default 0) is a no-op on the draw order.
    inline void stableSortDrawOrder(std::vector<VFXDrawOrderEntry>& entries)
    {
        std::stable_sort(entries.begin(), entries.end(), drawOrderLess);
    }
}
