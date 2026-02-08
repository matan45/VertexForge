#include "TerrainFileCache.hpp"
#include "TerrainTile.hpp"
#include "../resource/Types.hpp"
#include "../print/EditorLogger.hpp"

namespace terrain
{
    TerrainFileCache::TerrainFileCache(const std::string& filePath,
                                       const TerrainFileHeader& header,
                                       const std::vector<TileIndexEntry>& index)
        : filePath(filePath)
        , header(header)
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

        // If meshlet cache exists in file, load directly
        if (hasMeshletCache() && entry->meshletDataOffset != 0)
        {
            std::array<TileLODData, TERRAIN_LOD_COUNT> lodData;
            if (TerrainSerializer::readTileLODData(filePath, *entry, lodData))
            {
                size_t oldUsage = estimateTileRAMUsage(tile);
                tile.lodLevels = std::move(lodData);
                tile.isDirty = false;
                tile.dirtyLODMask = 0;
                // Only update bounds if heights are loaded; otherwise keep the
                // conservative metadata bounds from initializeMetadataOnly()
                if (tile.hasHeightData())
                    tile.updateWorldBounds();

                size_t newUsage = estimateTileRAMUsage(tile);
                currentRAMUsage += (newUsage - oldUsage);
                return true;
            }

            vfLogWarning("TerrainFileCache: Failed to read LOD cache for tile ({}, {}), falling back to regeneration",
                         tile.coord.x, tile.coord.z);
        }

        // Fallback: load heights and regenerate LODs
        if (!ensureHeightsLoaded(tile))
            return false;

        size_t oldUsage = estimateTileRAMUsage(tile);
        generator.generateAllLODs(tile, nullptr, getTile);

        size_t newUsage = estimateTileRAMUsage(tile);
        currentRAMUsage += (newUsage - oldUsage);
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

        size_t oldUsage = estimateTileRAMUsage(tile);

        std::vector<float> heights;
        if (!TerrainSerializer::readTileHeights(filePath, *entry, heights))
        {
            vfLogError("TerrainFileCache: Failed to read heights for tile ({}, {})", tile.coord.x, tile.coord.z);
            return false;
        }

        tile.initializeFromHeights(heights);

        // Also load weight data if present (always overwrite default-initialized weight maps
        // since file data is the authoritative source for loaded tiles)
        if (entry->weightDataOffset != 0)
        {
            TileWeightMapData weights;
            if (TerrainSerializer::readTileWeights(filePath, *entry, weights))
            {
                tile.weightMap = std::move(weights);
                tile.weightMapGPUDirty = true;
            }
        }

        size_t newUsage = estimateTileRAMUsage(tile);
        currentRAMUsage += (newUsage - oldUsage);
        return true;
    }

    void TerrainFileCache::evictTileGeometry(TerrainTile& tile)
    {
        // Never evict dirty tiles - they have in-memory modifications
        if (dirtyCoords.count(tile.coord))
            return;

        size_t oldUsage = estimateTileRAMUsage(tile);

        // Clear all LOD geometry
        for (auto& lod : tile.lodLevels)
        {
            lod.clear();
            // Force deallocation via swap idiom
            std::vector<resource::Vertex>().swap(lod.vertices);
            std::vector<uint32_t>().swap(lod.indices);
            std::vector<resource::Meshlet>().swap(lod.meshlets);
            std::vector<uint32_t>().swap(lod.meshletVertices);
            std::vector<uint32_t>().swap(lod.meshletPrimitives);
        }

        // Clear height data (can be reloaded from file)
        std::vector<float>().swap(tile.heightData);

        // Clear weight map data (can be reloaded from file)
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

    void TerrainFileCache::clearDirty(const TileCoord& coord)
    {
        dirtyCoords.erase(coord);
    }

    bool TerrainFileCache::isDirty(const TileCoord& coord) const
    {
        return dirtyCoords.count(coord) > 0;
    }

    bool TerrainFileCache::refreshIndex(const std::string& newPath)
    {
        TerrainFileHeader newHeader;
        std::vector<TileIndexEntry> newIndex;

        if (!TerrainSerializer::readHeader(newPath, newHeader, newIndex))
        {
            vfLogError("TerrainFileCache: Failed to refresh index from {}", newPath);
            return false;
        }

        filePath = newPath;
        header = newHeader;

        indexMap.clear();
        for (const auto& entry : newIndex)
        {
            TileCoord coord{entry.coordX, entry.coordZ};
            indexMap[coord] = entry;
        }

        dirtyCoords.clear();
        return true;
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

        // Height data
        usage += tile.heightData.capacity() * sizeof(float);

        // LOD geometry
        for (const auto& lod : tile.lodLevels)
        {
            usage += lod.vertices.capacity() * sizeof(resource::Vertex);
            usage += lod.indices.capacity() * sizeof(uint32_t);
            usage += lod.meshlets.capacity() * sizeof(resource::Meshlet);
            usage += lod.meshletVertices.capacity() * sizeof(uint32_t);
            usage += lod.meshletPrimitives.capacity() * sizeof(uint32_t);
        }

        // Weight map
        for (const auto& layer : tile.weightMap.layerWeights)
        {
            usage += layer.capacity() * sizeof(float);
        }

        return usage;
    }

} // namespace terrain
