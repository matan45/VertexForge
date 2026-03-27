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

    struct alignas(16) WaterTileGPUData
    {
        glm::vec4 worldOriginAndSize;   // xyz = tile world origin, w = tileSize
        glm::vec4 heightAndWave;        // x = waterHeight, y = 1.0, z = lodLevel, w = 0
    };

    struct WaterTileInfo
    {
        terrain::TileCoord coord;
        float waterHeight = 0.0f;
        uint64_t lastAccessFrame = 0;
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

        [[nodiscard]] static uint32_t selectLOD(float distance);
    };
}
