#include "TerrainFileCache.hpp"
#include "../print/Log.hpp"
#include "TerrainTile.hpp"
#include "../resource/Types.hpp"

namespace terrain
{
    TerrainFileCache::TerrainFileCache(const std::string& filePath,
                                       const TerrainFileHeader& header,
                                       const std::vector<TileIndexEntry>& index,
                                       uint64_t indexTableOffset)
        : filePath(filePath)
        , header(header)
        , indexTableOffset(indexTableOffset)
    {
        for (const auto& entry : index)
        {
            TileCoord coord{entry.coordX, entry.coordZ};
            indexMap[coord] = entry;
        }
    }

    bool TerrainFileCache::ensureLODsLoaded(TerrainTile& tile, TerrainTileGenerator& generator,
                                             const TileLookup& getTile)
    {
        if (tile.hasAnyLODData())
            return true;

        const TileIndexEntry* entry = findIndex(tile.coord);
        if (!entry)
        {
            vfLogError("TerrainFileCache: No index entry for tile ({}, {})", tile.coord.x, tile.coord.z);
            return false;
        }

        // Dynamically added tile not yet saved to disk
        if (entry->heightDataOffset == 0)
            return false;

        if (hasMeshletCache() && entry->meshletDataOffset != 0)
        {
            std::array<TileLODData, TERRAIN_LOD_COUNT> lodData;
            if (TerrainSerializer::readTileLODData(filePath, *entry, lodData))
            {
                size_t oldUsage = estimateTileRAMUsage(tile);
                tile.lodLevels = std::move(lodData);
                tile.isDirty = false;
                tile.dirtyLODMask = 0;
                if (tile.hasHeightData())
                    tile.updateWorldBounds();

                if (entry->weightDataOffset != 0)
                {
                    TileWeightMapData weights;
                    if (TerrainSerializer::readTileWeights(filePath, *entry, weights))
                    {
                        tile.weightMap = std::move(weights);
                        tile.weightMapGPUDirty = true;
                    }
                }

                if (entry->holeMaskDataOffset != 0)
                {
                    std::vector<uint8_t> holeMask;
                    if (TerrainSerializer::readTileHoleMask(filePath, *entry, holeMask))
                    {
                        tile.holeMask = std::move(holeMask);
                        tile.topologyDirty = true;
                    }
                }

                size_t newUsage = estimateTileRAMUsage(tile);
                if (newUsage >= oldUsage)
                    currentRAMUsage += (newUsage - oldUsage);
                else if (currentRAMUsage > (oldUsage - newUsage))
                    currentRAMUsage -= (oldUsage - newUsage);
                else
                    currentRAMUsage = 0;
                return true;
            }

            vfLogWarning("TerrainFileCache: Failed to read LOD cache for tile ({}, {}), falling back to regeneration",
                         tile.coord.x, tile.coord.z);
        }

        if (!ensureHeightsLoaded(tile))
            return false;

        size_t oldUsage = estimateTileRAMUsage(tile);
        generator.generateAllLODs(tile, nullptr, getTile);

        size_t newUsage = estimateTileRAMUsage(tile);
        if (newUsage >= oldUsage)
            currentRAMUsage += (newUsage - oldUsage);
        else if (currentRAMUsage > (oldUsage - newUsage))
            currentRAMUsage -= (oldUsage - newUsage);
        else
            currentRAMUsage = 0;
        return tile.hasAnyLODData();
    }

    bool TerrainFileCache::ensureHeightsLoaded(TerrainTile& tile)
    {
        if (tile.hasHeightData())
            return true;

        const TileIndexEntry* entry = findIndex(tile.coord);
        if (!entry)
        {
            vfLogError("TerrainFileCache: No index entry for tile ({}, {})", tile.coord.x, tile.coord.z);
            return false;
        }

        // Dynamically added tile not yet saved to disk
        if (entry->heightDataOffset == 0)
            return false;

        size_t oldUsage = estimateTileRAMUsage(tile);

        std::vector<float> heights;
        if (!TerrainSerializer::readTileHeights(filePath, *entry, heights))
        {
            vfLogError("TerrainFileCache: Failed to read heights for tile ({}, {})", tile.coord.x, tile.coord.z);
            return false;
        }

        tile.initializeFromHeights(heights);

        if (entry->weightDataOffset != 0)
        {
            TileWeightMapData weights;
            if (TerrainSerializer::readTileWeights(filePath, *entry, weights))
            {
                tile.weightMap = std::move(weights);
                tile.weightMapGPUDirty = true;
            }
        }

        if (entry->holeMaskDataOffset != 0)
        {
            std::vector<uint8_t> holeMask;
            if (TerrainSerializer::readTileHoleMask(filePath, *entry, holeMask))
            {
                tile.holeMask = std::move(holeMask);
                tile.topologyDirty = true;
            }
        }

        size_t newUsage = estimateTileRAMUsage(tile);
        if (newUsage >= oldUsage)
            currentRAMUsage += (newUsage - oldUsage);
        else if (currentRAMUsage > (oldUsage - newUsage))
            currentRAMUsage -= (oldUsage - newUsage);
        else
            currentRAMUsage = 0;
        return true;
    }

    void TerrainFileCache::evictTileGeometry(TerrainTile& tile)
    {
        if (dirtyCoords.count(tile.coord))
            return;

        size_t oldUsage = estimateTileRAMUsage(tile);

        for (auto& lod : tile.lodLevels)
        {
            lod.clear();
            std::vector<resource::Vertex>().swap(lod.vertices);
            std::vector<uint32_t>().swap(lod.indices);
            std::vector<resource::Meshlet>().swap(lod.meshlets);
            std::vector<uint32_t>().swap(lod.meshletVertices);
            std::vector<uint32_t>().swap(lod.meshletPrimitives);
        }

        std::vector<float>().swap(tile.heightData);
        std::vector<uint8_t>().swap(tile.holeMask);
        tile.weightMap = TileWeightMapData{};

        size_t newUsage = estimateTileRAMUsage(tile);
        if (oldUsage > newUsage)
            currentRAMUsage -= (oldUsage - newUsage);
        else
            currentRAMUsage = 0;
    }

    void TerrainFileCache::markDirty(const TileCoord& coord)
    {
        dirtyCoords.insert(coord);
    }

    void TerrainFileCache::addNewTileEntry(const TileCoord& coord)
    {
        TileIndexEntry entry{};
        entry.coordX = coord.x;
        entry.coordZ = coord.z;
        // Zero offsets indicate no file data yet
        entry.heightDataOffset = 0;
        entry.heightDataSize = 0;
        entry.weightDataOffset = 0;
        entry.meshletDataOffset = 0;
        entry.holeMaskDataOffset = 0;
        indexMap[coord] = entry;
        dirtyCoords.insert(coord);
        tilesAddedOrRemoved = true;
    }

    void TerrainFileCache::removeEntry(const TileCoord& coord)
    {
        indexMap.erase(coord);
        dirtyCoords.erase(coord);
        tilesAddedOrRemoved = true;
    }

    bool TerrainFileCache::refreshIndex(const std::string& newPath)
    {
        TerrainFileHeader newHeader;
        std::vector<TileIndexEntry> newIndex;
        uint64_t newIndexOffset = 0;

        if (!TerrainSerializer::readHeader(newPath, newHeader, newIndex, &newIndexOffset))
        {
            vfLogError("TerrainFileCache: Failed to refresh index from {}", newPath);
            return false;
        }

        filePath = newPath;
        header = newHeader;
        indexTableOffset = newIndexOffset;
        tilesAddedOrRemoved = false;

        indexMap.clear();
        for (const auto& entry : newIndex)
        {
            TileCoord coord{entry.coordX, entry.coordZ};
            indexMap[coord] = entry;
        }

        dirtyCoords.clear();
        return true;
    }

    std::vector<TileCoord> TerrainFileCache::getAvailableCoords() const
    {
        std::vector<TileCoord> coords;
        coords.reserve(indexMap.size());
        for (const auto& [coord, entry] : indexMap)
        {
            coords.push_back(coord);
        }
        return coords;
    }

    std::vector<TileCoord> TerrainFileCache::getSavedCoords() const
    {
        std::vector<TileCoord> coords;
        coords.reserve(indexMap.size());
        for (const auto& [coord, entry] : indexMap)
        {
            if (entry.heightDataOffset != 0)
                coords.push_back(coord);
        }
        return coords;
    }

    bool TerrainFileCache::hasCoord(const TileCoord& coord) const
    {
        return indexMap.find(coord) != indexMap.end();
    }

    bool TerrainFileCache::isTileDirty(const TileCoord& coord) const
    {
        return dirtyCoords.count(coord) > 0;
    }

    const std::unordered_set<TileCoord, TileCoordHash>& TerrainFileCache::getDirtyCoords() const
    {
        return dirtyCoords;
    }

    size_t TerrainFileCache::getDirtyCount() const
    {
        return dirtyCoords.size();
    }

    const std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash>& TerrainFileCache::getIndexMap() const
    {
        return indexMap;
    }

    bool TerrainFileCache::hasNewOrRemovedTiles() const
    {
        return tilesAddedOrRemoved;
    }

    void TerrainFileCache::clearDirtyCoords()
    {
        dirtyCoords.clear();
    }

    bool TerrainFileCache::hasMeshletCache() const
    {
        return hasFlag(header.flags, TerrainFormatFlags::HAS_MESHLET_CACHE);
    }

    const TileIndexEntry* TerrainFileCache::findIndex(const TileCoord& coord) const
    {
        auto it = indexMap.find(coord);
        if (it == indexMap.end())
            return nullptr;
        return &it->second;
    }

    size_t TerrainFileCache::estimateTileRAMUsage(const TerrainTile& tile) const
    {
        size_t usage = 0;
        usage += tile.heightData.capacity() * sizeof(float);
        usage += tile.holeMask.capacity() * sizeof(uint8_t);

        for (const auto& lod : tile.lodLevels)
        {
            usage += lod.vertices.capacity() * sizeof(resource::Vertex);
            usage += lod.indices.capacity() * sizeof(uint32_t);
            usage += lod.meshlets.capacity() * sizeof(resource::Meshlet);
            usage += lod.meshletVertices.capacity() * sizeof(uint32_t);
            usage += lod.meshletPrimitives.capacity() * sizeof(uint32_t);
        }

        for (const auto& layer : tile.weightMap.layerWeights)
        {
            usage += layer.capacity() * sizeof(float);
        }

        return usage;
    }

} // namespace terrain
