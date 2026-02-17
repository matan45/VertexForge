#pragma once

#include "WaterTile.hpp"
#include "WaterTypes.hpp"
#include <unordered_map>
#include <vector>
#include <memory>

namespace water
{
    class WaterGrid
    {
    private:
        WaterTileConfig config;
        float defaultWaterHeight = 0.0f;
        std::unordered_map<TileCoord, std::unique_ptr<WaterTile>, TileCoordHash> tiles;

    public:
        explicit WaterGrid(const WaterTileConfig& config, float waterHeight = 0.0f);
        ~WaterGrid() = default;

        void createGrid(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ);

        [[nodiscard]] WaterTile* getOrCreateTile(const TileCoord& coord);
        [[nodiscard]] WaterTile* getTile(const TileCoord& coord);
        [[nodiscard]] const WaterTile* getTile(const TileCoord& coord) const;
        void removeTile(const TileCoord& coord);

        [[nodiscard]] std::vector<WaterTile*> getVisibleTiles(const math::Frustum& frustum);
        [[nodiscard]] std::vector<WaterTile*> getAllTiles();
        [[nodiscard]] std::vector<const WaterTile*> getAllTiles() const;
        [[nodiscard]] size_t getTileCount() const;

        [[nodiscard]] float getWaterHeightAt(const glm::vec2& worldXZ) const;
        [[nodiscard]] bool isPositionInWater(const glm::vec3& worldPos) const;

        [[nodiscard]] const WaterTileConfig& getConfig() const;
        [[nodiscard]] float getDefaultWaterHeight() const;
    };
}
