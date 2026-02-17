#include "WaterGrid.hpp"
#include <cmath>
#include <limits>

namespace water
{
    WaterGrid::WaterGrid(const WaterTileConfig& config, float waterHeight)
        : config(config)
        , defaultWaterHeight(waterHeight)
    {
    }

    void WaterGrid::createGrid(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ)
    {
        for (int32_t z = minZ; z <= maxZ; ++z)
        {
            for (int32_t x = minX; x <= maxX; ++x)
            {
                TileCoord coord(x, z);
                auto tile = std::make_unique<WaterTile>(coord, config.worldTileSize, defaultWaterHeight);
                tiles.emplace(coord, std::move(tile));
            }
        }
    }

    WaterTile* WaterGrid::getOrCreateTile(const TileCoord& coord)
    {
        auto it = tiles.find(coord);
        if (it != tiles.end())
            return it->second.get();

        auto tile = std::make_unique<WaterTile>(coord, config.worldTileSize, defaultWaterHeight);
        WaterTile* ptr = tile.get();
        tiles.emplace(coord, std::move(tile));
        return ptr;
    }

    WaterTile* WaterGrid::getTile(const TileCoord& coord)
    {
        auto it = tiles.find(coord);
        return (it != tiles.end()) ? it->second.get() : nullptr;
    }

    const WaterTile* WaterGrid::getTile(const TileCoord& coord) const
    {
        auto it = tiles.find(coord);
        return (it != tiles.end()) ? it->second.get() : nullptr;
    }

    void WaterGrid::removeTile(const TileCoord& coord)
    {
        tiles.erase(coord);
    }

    std::vector<WaterTile*> WaterGrid::getVisibleTiles(const math::Frustum& frustum)
    {
        std::vector<WaterTile*> result;
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

    std::vector<WaterTile*> WaterGrid::getAllTiles()
    {
        std::vector<WaterTile*> result;
        result.reserve(tiles.size());
        for (auto& [coord, tile] : tiles)
            result.push_back(tile.get());
        return result;
    }

    std::vector<const WaterTile*> WaterGrid::getAllTiles() const
    {
        std::vector<const WaterTile*> result;
        result.reserve(tiles.size());
        for (const auto& [coord, tile] : tiles)
            result.push_back(tile.get());
        return result;
    }

    size_t WaterGrid::getTileCount() const
    {
        return tiles.size();
    }

    float WaterGrid::getWaterHeightAt(const glm::vec2& worldXZ) const
    {
        int32_t tileX = static_cast<int32_t>(std::floor(worldXZ.x / config.worldTileSize));
        int32_t tileZ = static_cast<int32_t>(std::floor(worldXZ.y / config.worldTileSize));

        const WaterTile* tile = getTile(TileCoord(tileX, tileZ));
        if (tile)
            return tile->waterHeight;

        return -std::numeric_limits<float>::max();
    }

    bool WaterGrid::isPositionInWater(const glm::vec3& worldPos) const
    {
        int32_t tileX = static_cast<int32_t>(std::floor(worldPos.x / config.worldTileSize));
        int32_t tileZ = static_cast<int32_t>(std::floor(worldPos.z / config.worldTileSize));

        const WaterTile* tile = getTile(TileCoord(tileX, tileZ));
        if (!tile)
            return false;

        return worldPos.y <= tile->waterHeight;
    }

    const WaterTileConfig& WaterGrid::getConfig() const
    {
        return config;
    }

    float WaterGrid::getDefaultWaterHeight() const
    {
        return defaultWaterHeight;
    }
}
