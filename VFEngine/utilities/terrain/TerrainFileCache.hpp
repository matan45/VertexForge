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

        // Ensure tile has LOD meshlet data loaded in RAM (for GPU upload).
        // If meshlet cache exists in file, loads directly.
        // Otherwise loads heights and regenerates LODs via generator.
        bool ensureLODsLoaded(TerrainTile& tile, TerrainTileGenerator& generator,
                              const TileLookup& getTile = nullptr);

        // Ensure tile has heightData loaded in RAM (for sculpting / LOD regeneration).
        // Also loads weight data if present in the file.
        bool ensureHeightsLoaded(TerrainTile& tile);

        // Release tile's heavy geometry data from RAM.
        // Clears lodLevels. Clears heightData only if tile is not dirty.
        void evictTileGeometry(TerrainTile& tile);

        // Mark tile as dirty (modified in memory, stale on disk). Never evict+reload.
        void markDirty(const TileCoord& coord);
        void clearDirty(const TileCoord& coord);
        bool isDirty(const TileCoord& coord) const;

        // Re-read header + index after a save. Clears all dirty markers.
        bool refreshIndex(const std::string& newPath);

        [[nodiscard]] bool hasMeshletCache() const;
        [[nodiscard]] const std::string& getFilePath() const { return filePath; }
        [[nodiscard]] size_t getCurrentRAMUsage() const { return currentRAMUsage; }

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
