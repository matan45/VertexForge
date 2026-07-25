#pragma once

#include "../terrain/TerrainTypes.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace water
{
    constexpr uint32_t WATER_TILE_LOD_COUNT = 4;
    constexpr uint32_t MAX_WATER_GPU_INSTANCES = 128;

    // Hand-synced twin: struct WaterTileData in resources/shaders/water/water.glsl. Change both in
    // one edit - there is no codegen, and the SDK copy under sdk/include mirrors this file byte for
    // byte.
    //
    // VK-1607 repurposed two fields that were previously always written as constants:
    //   worldOriginAndSize.y  was an unused origin Y (the surface Y comes from heightAndWave.x)
    //                         and is now the tile's size along Z, so a water body's tile can be
    //                         exactly its (non-square) rectangle.
    //   heightAndWave.w       was always 0 and now carries the per-tile flags below.
    // Ocean tiles must therefore write .y = tileSize and .w = WATER_TILE_OCEAN_FLAGS; leaving either
    // at 0 gives zero-width, band-less ocean.
    struct alignas(16) WaterTileGPUData
    {
        glm::vec4 worldOriginAndSize;   // x,z = tile world origin XZ, w = size along X, y = size along Z
        glm::vec4 heightAndWave;        // x = waterHeight, y = 1.0, z = lodLevel, w = tile flags
    };

    // VK-1607: bits packed into WaterTileGPUData::heightAndWave.w. Bits 0..2 are the tile's own band
    // mask, ANDed with the global pc.bandEnableMask in the vertex shader; bit 3 marks a water-body
    // tile, which the fragment stage uses to exempt it from the ocean clip test. Twin: water.glsl.
    constexpr uint32_t WATER_TILE_BAND_MASK_BITS = 0x7u;
    constexpr uint32_t WATER_TILE_IS_BODY = 0x8u;

    // What an ocean tile carries: every band enabled, not a body. The global band mask stays the
    // only thing gating ocean bands.
    constexpr uint32_t WATER_TILE_OCEAN_FLAGS = WATER_TILE_BAND_MASK_BITS;

    // ---------------------------------------------------------------------------------------------
    // Per-tile simulation LOD
    //
    // The water vertex shader skips the expensive bands on distant tiles. Anything that has to agree
    // with the RENDERED surface - CPU buoyancy above all - has to skip exactly the same ones, or a
    // floating body bobs on waves that were never drawn under it.
    //
    // Twin: resources/shaders/water/water.glsl (the vertex stage's tileBandMask). One rule, two
    // languages, no codegen - change both in one edit.
    // ---------------------------------------------------------------------------------------------

    // Bands a tile at this LOD actually simulates. Bit 2 (ripples) goes at LOD 2, bit 1 (agitation)
    // at LOD 3; bit 0 (swell) is never dropped, which is why the pre-VK-1604 CPU sampler - which
    // only ever read band 0 - never diverged from the shader.
    [[nodiscard]] inline constexpr uint32_t lodBandMask(uint32_t bandMask, uint32_t lodLevel)
    {
        if (lodLevel >= 2u) bandMask &= ~4u;
        if (lodLevel >= 3u) bandMask &= ~2u;
        return bandMask;
    }

    // Editor-mode LOD: the 9x9 grid is bucketed by Chebyshev ring around the camera's tile.
    // Reproduces GPUDrivenRenderer::updateWater's ring bucketing exactly.
    [[nodiscard]] inline constexpr uint32_t editorRingLod(int ring)
    {
        if (ring <= 1) return 0u;
        if (ring <= 2) return 1u;
        if (ring <= 3) return 2u;
        return 3u;
    }

    // World-mode LOD by distance from the camera to the tile centre.
    //
    // CAVEAT for CPU callers: buildGPUTileData additionally clamps each tile to its neighbours'
    // LOD + 1 to close T-junction cracks, and that pass can only ever LOWER a tile's LOD. A CPU
    // query therefore matches the GPU everywhere except on the handful of tiles the crack-clamp
    // refined, where it may drop a band the GPU kept. That is strictly better than not mirroring the
    // rule at all (which diverges on every tile past ring 2), and reproducing it exactly would mean
    // publishing the whole per-tile map across the render/physics thread boundary.
    [[nodiscard]] inline uint32_t selectTileLod(float distance, float tileWorldSize)
    {
        if (tileWorldSize <= 0.0f)
            return 0u;
        if (distance < tileWorldSize * 2.0f) return 0u;
        if (distance < tileWorldSize * 5.0f) return 1u;
        if (distance < tileWorldSize * 10.0f) return 2u;
        return 3u;
    }

    struct WaterTileInfo
    {
        terrain::TileCoord coord;
        float waterHeight = 0.0f;
    };

    class WaterTileGrid
    {
    public:
        void addTile(terrain::TileCoord coord, float waterHeight);
        void removeTile(terrain::TileCoord coord);
        void clear();

        [[nodiscard]] bool hasTile(terrain::TileCoord coord) const;
        [[nodiscard]] size_t tileCount() const { return tiles.size(); }

        void buildGPUTileData(
            const glm::vec3& cameraPos,
            float tileWorldSize,
            float waterHeight,
            std::vector<WaterTileGPUData>& outTiles,
            uint32_t outLodCounts[WATER_TILE_LOD_COUNT]) const;

    private:
        std::unordered_map<terrain::TileCoord, WaterTileInfo, terrain::TileCoordHash> tiles;

        [[nodiscard]] static uint32_t selectLOD(float distance, float tileWorldSize);
    };
}
