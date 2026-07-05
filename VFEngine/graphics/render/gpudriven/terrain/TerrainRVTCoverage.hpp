#pragma once

#include <vector>

// ============================================================================
// Virtual Texturing (VK-1209) — terrain RVT coverage test (pure, device-free).
//
// Decides whether a virtual page's world-XZ rect is backed by loaded terrain. The
// residency gate uses it to avoid spending the per-frame page budget and physical
// tiles on pages that would bake fully "uncovered" (alpha 0) — those fragments render
// via the live composite fallback instead. The per-tile overlap test mirrors the loop
// in GPUDrivenRenderer::bakeTerrainRVT so the gate and the bake agree on which pages
// get real content. Header-only and free of Vulkan/glm/graphics types so it is
// unit-tested by the CPU-only Tests project (see test_vt_coverage.cpp).
// ============================================================================

namespace render::gpudriven
{
    // Axis-aligned rectangle in the terrain XZ plane, (minX, minZ) .. (maxX, maxZ).
    struct TerrainCoverageRect
    {
        float minX = 0.0f;
        float minZ = 0.0f;
        float maxX = 0.0f;
        float maxZ = 0.0f;
    };

    // Half-open overlap: rectangles sharing only an edge do NOT overlap. Matches the
    // `qMin.x >= qMax.x || qMin.y >= qMax.y` reject in bakeTerrainRVT.
    [[nodiscard]] inline bool terrainRectsOverlap(const TerrainCoverageRect& a,
                                                  const TerrainCoverageRect& b) noexcept
    {
        return a.minX < b.maxX && b.minX < a.maxX &&
               a.minZ < b.maxZ && b.minZ < a.maxZ;
    }

    // Is `inner` fully contained within `outer`?
    [[nodiscard]] inline bool terrainRectContains(const TerrainCoverageRect& outer,
                                                  const TerrainCoverageRect& inner) noexcept
    {
        return inner.minX >= outer.minX && inner.maxX <= outer.maxX &&
               inner.minZ >= outer.minZ && inner.maxZ <= outer.maxZ;
    }

    // Does `query` (a border-expanded page rect) have loaded-terrain coverage?
    //   requireFull == false : true if `query` overlaps ANY tile (the page bakes some content).
    //   requireFull == true  : true only if `query` is fully inside a SINGLE tile (a conservative
    //                          approximation of full coverage that avoids partial-page seams; a page
    //                          straddling two tiles is treated as not-full even if jointly covered).
    [[nodiscard]] inline bool terrainRectCovered(const TerrainCoverageRect& query,
                                                 const std::vector<TerrainCoverageRect>& tiles,
                                                 bool requireFull) noexcept
    {
        for (const auto& t : tiles)
        {
            if (requireFull)
            {
                if (terrainRectContains(t, query))
                    return true;
            }
            else if (terrainRectsOverlap(query, t))
            {
                return true;
            }
        }
        return false;
    }
}
