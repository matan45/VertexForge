#pragma once
#include <cmath>
#include <cstdint>

// VK-1610 terrain Runtime Virtual Texture pool geometry.
//
// The RVT atlas is sized from a VRAM budget in MB, but what actually decides whether the
// camera thrashes is the resident PAGE COUNT that budget buys — and that count depends on the
// plane layout. Turning per-layer detail maps on adds a tangent-normal plane and an HDR
// emission plane, taking the pool from 8 to 20 bytes per texel, so the same 128 MB drops from
// 1024 resident pages to 400. Nothing surfaced that before, which is exactly how a budget
// silently becomes too small.
//
// The math lives here, header-only and Vulkan-free, so all three consumers share one
// definition: Graphics (TerrainRVTLayout.hpp / TerrainRVTManager), the Editor's render-config
// UI (which must not include graphics headers), and the CPU-only Tests project. Same pattern
// as TerrainHeightBlend.hpp.
//
// render::vt::vtPoolDimForBudget() in the graphics module is the authority at runtime; the
// static_asserts in TerrainRVTLayout.hpp pin these constants against it so the two cannot drift.

namespace terrain
{
    // Physical tile edge in texels — mirrors render::vt::VT_PAGE_SIZE.
    inline constexpr uint32_t TERRAIN_RVT_PAGE_SIZE = 128u;

    // Hardware-safe upper bound on the atlas edge — mirrors render::vt::VT_MAX_POOL_DIM.
    inline constexpr uint32_t TERRAIN_RVT_MAX_POOL_DIM = 16384u;

    // Plane layout, mirroring terrainRVTLayout() in the graphics module:
    //   legacy: RGBA8 sRGB albedo+emissionStrength, RGBA8 unorm ORM            -> 4 + 4  =  8 B
    //   detail: + RGBA8 unorm tangent normal, + RGBA16F linear HDR emission    -> + 4 + 8 = 20 B
    inline constexpr uint32_t TERRAIN_RVT_LEGACY_PLANES = 2u;
    inline constexpr uint32_t TERRAIN_RVT_DETAIL_PLANES = 4u;
    inline constexpr uint32_t TERRAIN_RVT_LEGACY_BYTES_PER_TEXEL = 8u;
    inline constexpr uint32_t TERRAIN_RVT_DETAIL_BYTES_PER_TEXEL = 20u;

    // Below this many resident pages an RTS-height camera cannot keep its visible footprint
    // resident, so pages evict and re-bake every frame. Used only to decide whether to warn —
    // it is a heuristic floor, not a hard limit.
    inline constexpr uint32_t TERRAIN_RVT_MIN_HEALTHY_PAGES = 256u;

    [[nodiscard]] inline constexpr uint32_t terrainRVTPlaneCount(bool detailMaps) noexcept
    {
        return detailMaps ? TERRAIN_RVT_DETAIL_PLANES : TERRAIN_RVT_LEGACY_PLANES;
    }

    [[nodiscard]] inline constexpr uint32_t terrainRVTBytesPerTexel(bool detailMaps) noexcept
    {
        return detailMaps ? TERRAIN_RVT_DETAIL_BYTES_PER_TEXEL : TERRAIN_RVT_LEGACY_BYTES_PER_TEXEL;
    }

    struct TerrainRVTPoolGeometry
    {
        uint32_t planeCount = 0;
        uint32_t bytesPerTexel = 0;
        uint32_t poolDim = 0;       // atlas edge in texels (a multiple of TERRAIN_RVT_PAGE_SIZE)
        uint32_t capacityPages = 0; // resident tiles the atlas holds: (poolDim / pageSize)^2
        uint64_t bytes = 0;         // what the atlas actually costs, across all planes
    };

    // What `budgetMB` buys under `detailMaps`. Deliberately a byte-for-byte mirror of
    // render::vt::vtPoolDimForBudget(budgetMB, /*planes*/ 1, bytesPerTexel): floor the square
    // root of the affordable texel count, round DOWN to a whole page, then clamp to [1 page,
    // hardware max]. `planes` is 1 there because bytesPerTexel already sums every plane.
    [[nodiscard]] inline TerrainRVTPoolGeometry terrainRVTPoolGeometry(uint32_t budgetMB, bool detailMaps)
    {
        TerrainRVTPoolGeometry geo{};
        geo.planeCount = terrainRVTPlaneCount(detailMaps);
        geo.bytesPerTexel = terrainRVTBytesPerTexel(detailMaps);

        const uint64_t budgetBytes = static_cast<uint64_t>(budgetMB) << 20;
        const uint64_t maxTexels = budgetBytes / geo.bytesPerTexel;
        uint32_t dim = static_cast<uint32_t>(std::floor(std::sqrt(static_cast<double>(maxTexels))));
        dim = (dim / TERRAIN_RVT_PAGE_SIZE) * TERRAIN_RVT_PAGE_SIZE;
        if (dim < TERRAIN_RVT_PAGE_SIZE) dim = TERRAIN_RVT_PAGE_SIZE;
        if (dim > TERRAIN_RVT_MAX_POOL_DIM) dim = TERRAIN_RVT_MAX_POOL_DIM;

        geo.poolDim = dim;
        const uint32_t tilesPerSide = dim / TERRAIN_RVT_PAGE_SIZE;
        geo.capacityPages = tilesPerSide * tilesPerSide;
        // The clamps above mean this is what the pool costs, which is <= budget except when a
        // sub-one-page budget was rounded UP to the single-page floor.
        geo.bytes = static_cast<uint64_t>(dim) * dim * geo.bytesPerTexel;
        return geo;
    }
}
