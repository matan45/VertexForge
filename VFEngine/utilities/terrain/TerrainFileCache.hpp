#pragma once

#include "TerrainSerializer.hpp"
#include "TerrainTileGenerator.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace terrain
{
    class TerrainFileCache
    {
    public:
        TerrainFileCache(const std::string& filePath,
                         const TerrainFileHeader& header,
                         const std::vector<TileIndexEntry>& index);
        ~TerrainFileCache() = default;

        bool ensureLODsLoaded(TerrainTile& tile, TerrainTileGenerator& generator,
                              const TileLookup& getTile = nullptr);

        bool ensureHeightsLoaded(TerrainTile& tile);

        void evictTileGeometry(TerrainTile& tile);

        void markDirty(const TileCoord& coord);

        bool refreshIndex(const std::string& newPath);

        [[nodiscard]] bool hasMeshletCache() const;
        [[nodiscard]] const std::string& getFilePath() const { return filePath; }

    private:
        std::string filePath;
        TerrainFileHeader header;
        std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash> indexMap;
        std::unordered_set<TileCoord, TileCoordHash> dirtyCoords;
        size_t currentRAMUsage = 0;

        [[nodiscard]] const TileIndexEntry* findIndex(const TileCoord& coord) const;
        [[nodiscard]] size_t estimateTileRAMUsage(const TerrainTile& tile) const;
    };

} // namespace terrain
