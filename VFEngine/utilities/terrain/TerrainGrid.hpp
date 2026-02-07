#pragma once

#include "TerrainTile.hpp"
#include "TerrainTileGenerator.hpp"
#include <unordered_map>
#include <vector>
#include <memory>

namespace terrain
{
    class TerrainGrid
    {
    private:
        TerrainTileConfig config;
        std::unique_ptr<TerrainTileGenerator> generator;
        std::unordered_map<TileCoord, std::unique_ptr<TerrainTile>, TileCoordHash> tiles;

    public:
        explicit TerrainGrid(const TerrainTileConfig& config);
        ~TerrainGrid() = default;

        void setHeightSampler(HeightSampler sampler);

        [[nodiscard]] TerrainTile* getTile(const TileCoord& coord);
        [[nodiscard]] const TerrainTile* getTile(const TileCoord& coord) const;

        [[nodiscard]] std::vector<TerrainTile*> getVisibleTiles(const math::Frustum& frustum);

        // Returns coordinates of tiles whose LOD or stitching state changed
        [[nodiscard]] std::vector<TileCoord> updateLODs(const glm::vec3& cameraPosition);

        // Regenerate meshlets for dirty tiles (active LOD + one additional per frame)
        void regenerateDirtyTiles(const glm::vec3& cameraPosition);

        [[nodiscard]] std::vector<TerrainTile*> getAllTiles();
        [[nodiscard]] std::vector<const TerrainTile*> getAllTiles() const;
        [[nodiscard]] size_t getTileCount() const { return tiles.size(); }

        void createGrid(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ,
                        ProgressCallback progress = nullptr);

        // Weight map management
        void initializeWeightMaps(uint8_t layerCount);
        void updateWeightMapLayerCount(uint8_t newLayerCount);
        [[nodiscard]] std::vector<TerrainTile*> getWeightMapDirtyTiles();

    private:
        [[nodiscard]] TerrainTile* getOrCreateTile(const TileCoord& coord);

        void updateNeighborReferences(TerrainTile& tile);
        void updateAllNeighborReferences();
    };

} // namespace terrain
