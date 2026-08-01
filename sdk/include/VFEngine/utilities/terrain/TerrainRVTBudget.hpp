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
    //   world height (VK-1620): + R16 unorm normalized terrain surface Y       -> + 2      =  2 B
    inline constexpr uint32_t TERRAIN_RVT_LEGACY_PLANES = 2u;
    inline constexpr uint32_t TERRAIN_RVT_DETAIL_PLANES = 4u;
    inline constexpr uint32_t TERRAIN_RVT_LEGACY_BYTES_PER_TEXEL = 8u;
    inline constexpr uint32_t TERRAIN_RVT_DETAIL_BYTES_PER_TEXEL = 20u;

    // VK-1620 world-height plane. It is a SECOND, INDEPENDENT axis: it rides either the legacy or
    // the detail layout, so there are four combinations, not five. Always appended LAST so plane
    // indices 0-3 keep their meaning and nothing that samples the existing planes has to move.
    //
    // The format is R16_UNORM and there is deliberately NO fallback: a second byte count would make
    // the Editor's page-count readout a guess (it cannot know which format the device picked) and
    // silently desync it from the pool the engine actually builds. If a device cannot render to
    // R16_UNORM the feature turns itself off instead — see terrainRVTWorldHeightSupported() in
    // TerrainRVTLayout.hpp. 2 bytes is what UE5 spends too (its World Height RVT is R16_UNORM,
    // "stored normalized in the range of the minimum and maximum z coordinate from the RVT volume").
    inline constexpr uint32_t TERRAIN_RVT_WORLD_HEIGHT_PLANES = 1u;
    inline constexpr uint32_t TERRAIN_RVT_WORLD_HEIGHT_BYTES_PER_TEXEL = 2u;

    // Below this many resident pages an RTS-height camera cannot keep its visible footprint
    // resident, so pages evict and re-bake every frame. Used only to decide whether to warn —
    // it is a heuristic floor, not a hard limit.
    inline constexpr uint32_t TERRAIN_RVT_MIN_HEALTHY_PAGES = 256u;

    [[nodiscard]] inline constexpr uint32_t terrainRVTPlaneCount(bool detailMaps, bool worldHeight) noexcept
    {
        return (detailMaps ? TERRAIN_RVT_DETAIL_PLANES : TERRAIN_RVT_LEGACY_PLANES)
             + (worldHeight ? TERRAIN_RVT_WORLD_HEIGHT_PLANES : 0u);
    }

    [[nodiscard]] inline constexpr uint32_t terrainRVTBytesPerTexel(bool detailMaps, bool worldHeight) noexcept
    {
        return (detailMaps ? TERRAIN_RVT_DETAIL_BYTES_PER_TEXEL : TERRAIN_RVT_LEGACY_BYTES_PER_TEXEL)
             + (worldHeight ? TERRAIN_RVT_WORLD_HEIGHT_BYTES_PER_TEXEL : 0u);
    }

    // --- World-height plane encoding (VK-1620) -------------------------------
    // The plane stores the terrain's world Y renormalized into [0,1] over the terrain's AUTHORED
    // height range (TerrainTileConfig::minHeight/maxHeight), NOT over the bounds of whatever tiles
    // happen to be loaded. That distinction is load-bearing: RVT pages bake once and are then read
    // for as long as they stay resident, so a range that drifted as tiles streamed in would leave
    // every already-baked page decoding to a wrong world Y — a silent, camera-dependent error.
    //
    // Shared by the bake (encode), the scene-mesh blend (decode) and the CPU test that pins them
    // against each other. R16_UNORM over the default -10..100 range quantizes to 110/65535 = 1.68 mm.
    [[nodiscard]] inline constexpr float terrainRVTEncodeHeight(float worldY, float minHeight, float maxHeight) noexcept
    {
        const float range = maxHeight - minHeight;
        if (range <= 0.0f)
            return 0.0f;
        const float t = (worldY - minHeight) / range;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    [[nodiscard]] inline constexpr float terrainRVTDecodeHeight(float encoded, float minHeight, float maxHeight) noexcept
    {
        return minHeight + encoded * (maxHeight - minHeight);
    }

    // Interpolate a terrain height inside one grid quad the way the terrain is actually TRIANGULATED.
    //
    // TerrainTileGenerator::generateIndices emits (topLeft, bottomLeft, topRight) +
    // (topRight, bottomLeft, bottomRight), i.e. every quad is split on the ANTI-DIAGONAL running
    // from (x+1,z) to (x,z+1). The rendered surface over a quad is therefore two planes hinged on
    // that edge, and plain bilinear does NOT reproduce it: down the hinge bilinear returns
    // (h00+h11)/2 where the surface gives (h10+h01)/2. On a ridge crest that difference is the full
    // amplitude of the ridge, and it shows up as props floating above or sinking into the crest
    // they are sitting on.
    //
    // Mirrors sampleTileHeight() in terrain_rvt_bake.glsl. fx/fz are the fractional position inside
    // the quad, both in [0,1].
    [[nodiscard]] inline constexpr float terrainQuadHeight(float h00, float h10, float h01, float h11,
                                                           float fx, float fz) noexcept
    {
        // Lower-left triangle is (0,0),(0,1),(1,0) => the fx + fz <= 1 half.
        return (fx + fz <= 1.0f)
            ? h00 + fx * (h10 - h00) + fz * (h01 - h00)
            : h11 + (1.0f - fx) * (h01 - h11) + (1.0f - fz) * (h10 - h11);
    }

    struct TerrainRVTPoolGeometry
    {
        uint32_t planeCount = 0;
        uint32_t bytesPerTexel = 0;
        uint32_t poolDim = 0;       // atlas edge in texels (a multiple of TERRAIN_RVT_PAGE_SIZE)
        uint32_t capacityPages = 0; // resident tiles the atlas holds: (poolDim / pageSize)^2
        uint64_t bytes = 0;         // what the atlas actually costs, across all planes
    };

    // What `budgetMB` buys under `detailMaps` + `worldHeight`. Deliberately a byte-for-byte mirror
    // of render::vt::vtPoolDimForBudget(budgetMB, /*planes*/ 1, bytesPerTexel): floor the square
    // root of the affordable texel count, round DOWN to a whole page, then clamp to [1 page,
    // hardware max]. `planes` is 1 there because bytesPerTexel already sums every plane.
    //
    // VK-1620: the world-height plane is only +2 B/texel, but because the pool is sized by AREA the
    // page count falls faster than the byte count suggests — at the shipped 128 MB it costs 1024 ->
    // 784 pages on the legacy layout and 400 -> 361 on the detail layout. That is why it is gated
    // behind a setting rather than always on.
    [[nodiscard]] inline TerrainRVTPoolGeometry terrainRVTPoolGeometry(uint32_t budgetMB, bool detailMaps,
                                                                      bool worldHeight)
    {
        TerrainRVTPoolGeometry geo{};
        geo.planeCount = terrainRVTPlaneCount(detailMaps, worldHeight);
        geo.bytesPerTexel = terrainRVTBytesPerTexel(detailMaps, worldHeight);

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
