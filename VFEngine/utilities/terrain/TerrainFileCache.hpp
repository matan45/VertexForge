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
    private:
        std::string filePath;
        TerrainFileHeader header;
        std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash> indexMap;
        std::unordered_set<TileCoord, TileCoordHash> dirtyCoords;
        uint64_t indexTableOffset = 0;
        bool tilesAddedOrRemoved = false;
        size_t currentRAMUsage = 0;
    public:
        explicit TerrainFileCache(const std::string& filePath,
                         const TerrainFileHeader& header,
                         const std::vector<TileIndexEntry>& index,
                         uint64_t indexTableOffset = 0);
        ~TerrainFileCache() = default;

        bool ensureLODsLoaded(TerrainTile& tile, TerrainTileGenerator& generator,
                              const TileLookup& getTile = nullptr);

        bool ensureHeightsLoaded(TerrainTile& tile);
        bool ensureCaveDataLoaded(TerrainTile& tile);

        void evictTileGeometry(TerrainTile& tile);

        void markDirty(const TileCoord& coord);

        void addNewTileEntry(const TileCoord& coord);
        void removeEntry(const TileCoord& coord);

        bool refreshIndex(const std::string& newPath);

        [[nodiscard]] bool hasMeshletCache() const;
        [[nodiscard]] const std::string& getFilePath() const { return filePath; }

        [[nodiscard]] std::vector<TileCoord> getAvailableCoords() const;
        [[nodiscard]] std::vector<TileCoord> getSavedCoords() const;
        [[nodiscard]] bool hasCoord(const TileCoord& coord) const;
        [[nodiscard]] bool isTileDirty(const TileCoord& coord) const;

        [[nodiscard]] const std::unordered_set<TileCoord, TileCoordHash>& getDirtyCoords() const;
        [[nodiscard]] size_t getDirtyCount() const;
        [[nodiscard]] uint64_t getIndexTableOffset() const { return indexTableOffset; }
        [[nodiscard]] const TerrainFileHeader& getHeader() const { return header; }
        [[nodiscard]] const std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash>& getIndexMap() const;
        [[nodiscard]] bool hasNewOrRemovedTiles() const;
        void clearDirtyCoords();

        template<typename F>
        void forEachSavedCoord(F&& func) const
        {
            for (const auto& [coord, entry] : indexMap)
            {
                if (entry.heightDataOffset != 0)
                    func(coord);
            }
        }

    private:

        [[nodiscard]] const TileIndexEntry* findIndex(const TileCoord& coord) const;
        [[nodiscard]] size_t estimateTileRAMUsage(const TerrainTile& tile) const;
    };

} // namespace terrain
