#pragma once

#include "TerrainTile.hpp"
#include "TerrainTileGenerator.hpp"
#include "TerrainSerializer.hpp"
#include "TerrainFileCache.hpp"
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
        std::shared_ptr<TerrainFileCache> fileCache;

    public:
        explicit TerrainGrid(const TerrainTileConfig& config);
        ~TerrainGrid() = default;

        void setHeightSampler(HeightSampler sampler);

        [[nodiscard]] TerrainTile* getTile(const TileCoord& coord);
        [[nodiscard]] const TerrainTile* getTile(const TileCoord& coord) const;

        [[nodiscard]] std::vector<TerrainTile*> getVisibleTiles(const math::Frustum& frustum);

        void regenerateDirtyTiles(const glm::vec3& cameraPosition);

        [[nodiscard]] std::vector<TerrainTile*> getAllTiles();
        [[nodiscard]] std::vector<const TerrainTile*> getAllTiles() const;
        [[nodiscard]] size_t getTileCount() const { return tiles.size(); }

        void createGrid(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ,
                        ProgressCallback progress = nullptr);

        bool loadFromSerialized(const std::vector<TileLoadResult>& loadedTiles,
                                ProgressCallback progress = nullptr);

        bool loadMetadataOnly(const TerrainFileHeader& header,
                              const std::vector<TileIndexEntry>& index);

        void setFileCache(std::shared_ptr<TerrainFileCache> cache) { fileCache = std::move(cache); }
        [[nodiscard]] std::shared_ptr<TerrainFileCache> getFileCache() const { return fileCache; }
        [[nodiscard]] TerrainTileGenerator& getGenerator() { return *generator; }

        TerrainTile* addTile(const TileCoord& coord);
        TerrainTile* addTileFromFile(const TileCoord& coord);
        bool removeTile(const TileCoord& coord);
        void computeBounds(int32_t& minX, int32_t& minZ, int32_t& maxX, int32_t& maxZ) const;
        [[nodiscard]] bool hasTile(const TileCoord& coord) const { return tiles.find(coord) != tiles.end(); }

        template<typename F>
        void forEachTile(F&& func) const
        {
            for (const auto& [coord, tile] : tiles)
            {
                if (tile)
                    func(*tile);
            }
        }

        void initializeWeightMaps();
        [[nodiscard]] std::vector<TerrainTile*> getWeightMapDirtyTiles();

    private:
        [[nodiscard]] TerrainTile* getOrCreateTile(const TileCoord& coord);

        void updateNeighborReferences(TerrainTile& tile);
        void updateAllNeighborReferences();
    };

} // namespace terrain
