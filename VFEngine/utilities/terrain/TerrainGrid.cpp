#include "TerrainGrid.hpp"
#include <algorithm>
#include <cmath>

namespace terrain
{
    TerrainGrid::TerrainGrid(const TerrainTileConfig& config)
        : config(config)
          , generator(std::make_unique<TerrainTileGenerator>(config))
    {
    }

    void TerrainGrid::setHeightSampler(HeightSampler sampler)
    {
        generator->setHeightSampler(std::move(sampler));
    }

    TerrainTile* TerrainGrid::getTile(const TileCoord& coord)
    {
        auto it = tiles.find(coord);
        return (it != tiles.end()) ? it->second.get() : nullptr;
    }

    const TerrainTile* TerrainGrid::getTile(const TileCoord& coord) const
    {
        auto it = tiles.find(coord);
        return (it != tiles.end()) ? it->second.get() : nullptr;
    }

    TerrainTile* TerrainGrid::getOrCreateTile(const TileCoord& coord)
    {
        auto it = tiles.find(coord);
        if (it != tiles.end())
        {
            return it->second.get();
        }

        auto tile = generator->generateTile(coord);
        TerrainTile* tilePtr = tile.get();
        tiles.emplace(coord, std::move(tile));

        updateNeighborReferences(*tilePtr);

        invalidateBoundsCache();
        return tilePtr;
    }

    TileCoord TerrainGrid::worldToTileCoord(float worldX, float worldZ) const
    {
        return TileCoord(
            static_cast<int32_t>(std::floor(worldX / config.worldTileSize)),
            static_cast<int32_t>(std::floor(worldZ / config.worldTileSize))
        );
    }

    std::vector<TerrainTile*> TerrainGrid::getVisibleTiles(const math::Frustum& frustum)
    {
        std::vector<TerrainTile*> result;
        result.reserve(tiles.size());

        for (auto& [coord, tile] : tiles)
        {
            if (frustum.intersectsAABB(tile->worldBounds))
            {
                tile->isVisible = true;
                result.push_back(tile.get());
            }
            else
            {
                tile->isVisible = false;
            }
        }

        return result;
    }

    void TerrainGrid::updateNeighborReferences(TerrainTile& tile)
    {
        for (uint8_t i = 0; i < 4; ++i)
        {
            TileEdge edge = static_cast<TileEdge>(i);
            TileCoord neighborCoord = tile.coord + TileCoord::getNeighborOffset(edge);

            TerrainTile* neighbor = getTile(neighborCoord);
            if (neighbor)
            {
                tile.setNeighbor(edge, neighborCoord, neighbor->currentLOD);
                neighbor->setNeighbor(TileCoord::getOppositeEdge(edge), tile.coord, tile.currentLOD);
            }
            else
            {
                tile.clearNeighbor(edge);
            }
        }
    }

    void TerrainGrid::updateAllNeighborReferences()
    {
        for (auto& [coord, tile] : tiles)
        {
            updateNeighborReferences(*tile);
        }
    }

    std::vector<TileCoord> TerrainGrid::updateLODs(const glm::vec3& cameraPosition)
    {
        std::vector<TileCoord> changedTiles;
        changedTiles.reserve(tiles.size() / 4);

        for (auto& [coord, tile] : tiles)
        {
            uint32_t newLOD = generator->calculateLOD(cameraPosition, *tile);
            if (newLOD != tile->currentLOD)
            {
                tile->currentLOD = static_cast<uint8_t>(newLOD);
                changedTiles.push_back(coord);
            }
        }

        updateAllNeighborReferences();

        // Update edge stitching and mark tiles dirty if stitching requirements changed
        for (auto& [coord, tile] : tiles)
        {
            generator->updateEdgeStitching(*tile);

            if (tile->stitchingChanged())
            {
                tile->isDirty = true;
                tile->saveStitchState();

                if (std::find(changedTiles.begin(), changedTiles.end(), coord) == changedTiles.end())
                {
                    changedTiles.push_back(coord);
                }
            }
        }

        return changedTiles;
    }

    std::vector<TerrainTile*> TerrainGrid::getAllTiles()
    {
        std::vector<TerrainTile*> result;
        result.reserve(tiles.size());

        for (auto& [coord, tile] : tiles)
        {
            result.push_back(tile.get());
        }

        return result;
    }

    std::vector<const TerrainTile*> TerrainGrid::getAllTiles() const
    {
        std::vector<const TerrainTile*> result;
        result.reserve(tiles.size());

        for (const auto& [coord, tile] : tiles)
        {
            result.push_back(tile.get());
        }

        return result;
    }

    void TerrainGrid::createGrid(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ,
                                 ProgressCallback progress)
    {
        int32_t totalTiles = (maxX - minX + 1) * (maxZ - minZ + 1);
        int32_t currentTile = 0;

        for (int32_t z = minZ; z <= maxZ; ++z)
        {
            for (int32_t x = minX; x <= maxX; ++x)
            {
                if (progress)
                {
                    progress(static_cast<float>(currentTile) / static_cast<float>(totalTiles),
                             "Creating tile (" + std::to_string(x) + ", " + std::to_string(z) + ")");
                }

                (void)getOrCreateTile(TileCoord(x, z));
                ++currentTile;
            }
        }

        if (progress)
        {
            progress(1.0f, "Complete");
        }
    }

    void TerrainGrid::invalidateBoundsCache()
    {
        boundsDirty = true;
    }
}
