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
