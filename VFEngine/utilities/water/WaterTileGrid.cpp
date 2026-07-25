#include "WaterTileGrid.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace water
{
    void WaterTileGrid::addTile(terrain::TileCoord coord, float waterHeight)
    {
        auto it = tiles.find(coord);
        if (it != tiles.end())
        {
            it->second.waterHeight = waterHeight;
            return;
        }

        WaterTileInfo info;
        info.coord = coord;
        info.waterHeight = waterHeight;
        tiles.emplace(coord, info);
    }

    void WaterTileGrid::removeTile(terrain::TileCoord coord)
    {
        tiles.erase(coord);
    }

    void WaterTileGrid::clear()
    {
        tiles.clear();
    }

    bool WaterTileGrid::hasTile(terrain::TileCoord coord) const
    {
        return tiles.contains(coord);
    }

    uint32_t WaterTileGrid::selectLOD(float distance, float tileWorldSize)
    {
        // LOD thresholds as multiples of tile size
        if (distance < tileWorldSize * 2.0f) return 0;
        if (distance < tileWorldSize * 5.0f) return 1;
        if (distance < tileWorldSize * 10.0f) return 2;
        return 3;
    }

    void WaterTileGrid::buildGPUTileData(
        const glm::vec3& cameraPos,
        float tileWorldSize,
        float waterHeight,
        std::vector<WaterTileGPUData>& outTiles,
        uint32_t outLodCounts[WATER_TILE_LOD_COUNT]) const
    {
        outTiles.clear();
        for (uint32_t i = 0; i < WATER_TILE_LOD_COUNT; ++i)
            outLodCounts[i] = 0;

        if (tiles.empty())
            return;

        struct TileWithDist
        {
            terrain::TileCoord coord;
            float distance;
            uint32_t lod;
        };

        std::vector<TileWithDist> sortedTiles;
        sortedTiles.reserve(tiles.size());

        for (const auto& [coord, info] : tiles)
        {
            float tileOriginX = static_cast<float>(coord.x) * tileWorldSize;
            float tileOriginZ = static_cast<float>(coord.z) * tileWorldSize;
            float tileCenterX = tileOriginX + tileWorldSize * 0.5f;
            float tileCenterZ = tileOriginZ + tileWorldSize * 0.5f;

            float dx = tileCenterX - cameraPos.x;
            float dz = tileCenterZ - cameraPos.z;
            float dist = std::sqrt(dx * dx + dz * dz);

            sortedTiles.push_back({coord, dist, selectLOD(dist, tileWorldSize)});
        }

        // Clamp LOD to neighbor LOD + 1 to prevent T-junction cracks
        // Build a quick lookup for LOD by coord
        std::unordered_map<terrain::TileCoord, uint32_t, terrain::TileCoordHash> lodMap;
        lodMap.reserve(sortedTiles.size());
        for (const auto& t : sortedTiles)
            lodMap[t.coord] = t.lod;

        bool changed = true;
        while (changed)
        {
            changed = false;
            for (auto& t : sortedTiles)
            {
                static const terrain::TileCoord offsets[] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
                for (const auto& off : offsets)
                {
                    terrain::TileCoord neighbor{t.coord.x + off.x, t.coord.z + off.z};
                    auto it = lodMap.find(neighbor);
                    if (it != lodMap.end() && t.lod > it->second + 1)
                    {
                        t.lod = it->second + 1;
                        lodMap[t.coord] = t.lod;
                        changed = true;
                    }
                }
            }
        }

        // Sort by distance and clamp to max instances
        std::sort(sortedTiles.begin(), sortedTiles.end(),
                  [](const TileWithDist& a, const TileWithDist& b) { return a.distance < b.distance; });

        if (sortedTiles.size() > MAX_WATER_GPU_INSTANCES)
            sortedTiles.resize(MAX_WATER_GPU_INSTANCES);

        // Bucket by LOD
        std::array<std::vector<WaterTileGPUData>, WATER_TILE_LOD_COUNT> lodBuckets;
        for (auto& bucket : lodBuckets) bucket.reserve(32);

        for (const auto& t : sortedTiles)
        {
            float tileOriginX = static_cast<float>(t.coord.x) * tileWorldSize;
            float tileOriginZ = static_cast<float>(t.coord.z) * tileWorldSize;

            // VK-1607: .y is the size along Z (square here) and .w carries the per-tile flags -
            // every band enabled, not a water body.
            WaterTileGPUData tile;
            tile.worldOriginAndSize = glm::vec4(tileOriginX, tileWorldSize, tileOriginZ, tileWorldSize);
            tile.heightAndWave = glm::vec4(waterHeight, 1.0f, static_cast<float>(t.lod),
                                           static_cast<float>(WATER_TILE_OCEAN_FLAGS));
            lodBuckets[t.lod].push_back(tile);
        }

        // Flatten into output sorted by LOD
        for (uint32_t lod = 0; lod < WATER_TILE_LOD_COUNT; ++lod)
        {
            outLodCounts[lod] = static_cast<uint32_t>(lodBuckets[lod].size());
            outTiles.insert(outTiles.end(), lodBuckets[lod].begin(), lodBuckets[lod].end());
        }
    }
}
