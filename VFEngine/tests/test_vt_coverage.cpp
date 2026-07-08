#include <doctest.h>
#include <render/gpudriven/terrain/TerrainRVTCoverage.hpp>

#include <vector>

// ============================================================================
// Terrain RVT coverage test (VK-1209 / VK-1480): decides whether a virtual page's
// world-XZ rect is backed by loaded terrain, so the residency gate can skip pages
// that would bake fully "uncovered" (the shader renders those via the live composite
// fallback). Pure, CPU-only — mirrors the per-tile overlap test in bakeTerrainRVT.
// ============================================================================

using namespace render::gpudriven;

namespace
{
    TerrainCoverageRect R(float minX, float minZ, float maxX, float maxZ)
    {
        return TerrainCoverageRect{minX, minZ, maxX, maxZ};
    }
}

TEST_CASE("VT coverage: rect overlap is half-open (shared edge is not overlap)")
{
    // Clear overlap.
    CHECK(terrainRectsOverlap(R(0, 0, 10, 10), R(5, 5, 15, 15)));
    // Disjoint in X.
    CHECK_FALSE(terrainRectsOverlap(R(0, 0, 10, 10), R(20, 0, 30, 10)));
    // Disjoint in Z.
    CHECK_FALSE(terrainRectsOverlap(R(0, 0, 10, 10), R(0, 20, 10, 30)));
    // Edge-sharing along X (a.maxX == b.minX) must NOT count as overlap — matches the
    // `qMin >= qMax` reject in bakeTerrainRVT so the gate and the bake agree.
    CHECK_FALSE(terrainRectsOverlap(R(0, 0, 10, 10), R(10, 0, 20, 10)));
    // A tiny interior overlap does count.
    CHECK(terrainRectsOverlap(R(0, 0, 10, 10), R(9.5f, 0, 20, 10)));
    // Overlap is symmetric.
    CHECK(terrainRectsOverlap(R(5, 5, 15, 15), R(0, 0, 10, 10)));
}

TEST_CASE("VT coverage: containment")
{
    CHECK(terrainRectContains(R(0, 0, 100, 100), R(10, 10, 20, 20)));
    // Flush against the outer edges still counts as contained.
    CHECK(terrainRectContains(R(0, 0, 100, 100), R(0, 0, 100, 100)));
    // One corner pokes outside.
    CHECK_FALSE(terrainRectContains(R(0, 0, 100, 100), R(90, 90, 110, 110)));
    // Fully outside.
    CHECK_FALSE(terrainRectContains(R(0, 0, 100, 100), R(200, 200, 210, 210)));
}

TEST_CASE("VT coverage: any-overlap gating (requireFull = false)")
{
    const std::vector<TerrainCoverageRect> tiles = {
        R(0, 0, 100, 100),
        R(100, 0, 200, 100),
    };

    // A page inside the first tile is covered.
    CHECK(terrainRectCovered(R(10, 10, 20, 20), tiles, /*requireFull*/ false));
    // A page straddling the two tiles overlaps both — covered (some content bakes).
    CHECK(terrainRectCovered(R(90, 10, 110, 20), tiles, /*requireFull*/ false));
    // A page entirely outside every tile is NOT covered -> stays non-resident -> composite fallback.
    CHECK_FALSE(terrainRectCovered(R(300, 300, 320, 320), tiles, /*requireFull*/ false));
    // No loaded tiles -> nothing is covered (the first-frame / nothing-streamed case).
    CHECK_FALSE(terrainRectCovered(R(10, 10, 20, 20), {}, /*requireFull*/ false));
    // Edge-adjacent-only page (touches the seam but no interior overlap) is not covered.
    CHECK_FALSE(terrainRectCovered(R(200, 10, 210, 20), tiles, /*requireFull*/ false));
}

TEST_CASE("VT coverage: full-coverage gating (requireFull = true)")
{
    const std::vector<TerrainCoverageRect> tiles = {
        R(0, 0, 100, 100),
        R(100, 0, 200, 100),
    };

    // Fully inside a single tile -> fully covered.
    CHECK(terrainRectCovered(R(10, 10, 20, 20), tiles, /*requireFull*/ true));
    // Straddling two adjacent tiles: jointly covered, but the single-tile approximation treats it
    // as NOT full (conservative -> falls back to the composite, never a partial-page black seam).
    CHECK_FALSE(terrainRectCovered(R(90, 10, 110, 20), tiles, /*requireFull*/ true));
    // Partially outside all tiles -> not full.
    CHECK_FALSE(terrainRectCovered(R(90, 90, 130, 130), tiles, /*requireFull*/ true));
    // Empty tile set -> not full.
    CHECK_FALSE(terrainRectCovered(R(10, 10, 20, 20), {}, /*requireFull*/ true));
}
