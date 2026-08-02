#include "TerrainSerializer.hpp"
#include "TerrainCompression.hpp"
#include "TerrainFileAccess.hpp"
#include "../print/Log.hpp"
#include "TerrainGrid.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace terrain
{
    namespace fs = std::filesystem;

    TerrainFormatFlags TerrainSerializer::computeFlags(const TerrainGrid& grid,
                                                       const TerrainPhysicsConfig& physicsConfig,
                                                       const TerrainStreamingConfig& streamingConfig)
    {
        TerrainFormatFlags flags = TerrainFormatFlags::HAS_COMPRESSED_DATA;

        auto allTiles = grid.getAllTiles();

        for (const auto* tile : allTiles)
        {
            if (tile->weightMap.isInitialized())
            {
                flags = flags | TerrainFormatFlags::HAS_WEIGHT_MAPS;
                break;
            }
        }
        for (const auto* tile : allTiles)
        {
            if (!tile->lodLevels.empty() && tile->lodLevels[0].hasMeshlets())
            {
                flags = flags | TerrainFormatFlags::HAS_MESHLET_CACHE;
                break;
            }
        }
        if (physicsConfig.hasCollider)
            flags = flags | TerrainFormatFlags::HAS_PHYSICS_DATA;
        if (streamingConfig.enabled)
            flags = flags | TerrainFormatFlags::HAS_STREAMING_CONFIG;

        for (const auto* tile : allTiles)
        {
            if (tile->hasHoleMask())
            {
                bool hasAnyHole = false;
                for (uint8_t h : tile->holeMask)
                {
                    if (h) { hasAnyHole = true; break; }
                }
                if (hasAnyHole)
                {
                    flags = flags | TerrainFormatFlags::HAS_HOLE_MASK;
                    break;
                }
            }
        }

        for (const auto* tile : allTiles)
        {
            if (tile->hasCaveData() && tile->caveData->hasCaveGeometry())
            {
                flags = flags | TerrainFormatFlags::HAS_CAVE_DATA;
                break;
            }
        }

        return flags;
    }

    bool TerrainSerializer::validateIncrementalFlags(TerrainFormatFlags currentFlags,
                                                      TerrainFormatFlags newFlags)
    {
        // If optional header sections toggled, header size changed -- fall back to full save.
        // NOTE: TerrainService::prepareSaveIncremental() has a matching guard on the main thread.
        // Both must agree -- if updating one, update the other.
        bool hadPhysics = hasFlag(currentFlags, TerrainFormatFlags::HAS_PHYSICS_DATA);
        bool hasPhysicsNow = hasFlag(newFlags, TerrainFormatFlags::HAS_PHYSICS_DATA);
        bool hadStreaming = hasFlag(currentFlags, TerrainFormatFlags::HAS_STREAMING_CONFIG);
        bool hasStreamingNow = hasFlag(newFlags, TerrainFormatFlags::HAS_STREAMING_CONFIG);

        if (hadPhysics != hasPhysicsNow || hadStreaming != hasStreamingNow)
        {
            vfLogWarning("TerrainSerializer: Header size changed, falling back to full save");
            return false;
        }
        return true;
    }

    std::vector<TileIndexEntry> TerrainSerializer::buildSortedIndex(
        const std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash>& indexMap)
    {
        std::vector<TileIndexEntry> entries;
        entries.reserve(indexMap.size());
        for (const auto& [coord, entry] : indexMap)
            entries.push_back(entry);

        std::sort(entries.begin(), entries.end(),
            [](const TileIndexEntry& a, const TileIndexEntry& b) {
                if (a.coordX != b.coordX) return a.coordX < b.coordX;
                return a.coordZ < b.coordZ;
            });
        return entries;
    }

    bool TerrainSerializer::writeIncrementalTiles(std::ostream& file,
                                                   const TerrainGrid& grid,
                                                   const std::unordered_set<TileCoord, TileCoordHash>& dirtyCoords,
                                                   TerrainFormatFlags flags,
                                                   std::vector<TileIndexEntry>& indexEntries)
    {
        for (const auto& coord : dirtyCoords)
        {
            const TerrainTile* tile = grid.getTile(coord);
            if (!tile)
            {
                vfLogWarning("TerrainSerializer: Dirty tile ({}, {}) not in grid, skipping",
                             coord.x, coord.z);
                continue;
            }

            TileIndexEntry newEntry{};
            if (!writeTileData(file, *tile, flags, newEntry))
            {
                vfLogError("TerrainSerializer: Failed to write dirty tile ({}, {})",
                           coord.x, coord.z);
                return false;
            }

            for (auto& entry : indexEntries)
            {
                if (entry.coordX == coord.x && entry.coordZ == coord.z)
                {
                    entry = newEntry;
                    break;
                }
            }
        }
        return true;
    }

    bool TerrainSerializer::writeIncrementalHeaderAndIndex(std::ostream& file,
                                                            const TerrainFileHeader& updatedHeader,
                                                            uint64_t indexTableOffset,
                                                            const std::vector<TileIndexEntry>& indexEntries)
    {
        file.seekp(0, std::ios::beg);
        if (!writeHeader(file, updatedHeader))
        {
            vfLogError("TerrainSerializer: Failed to rewrite header");
            return false;
        }

        file.seekp(static_cast<std::streamoff>(indexTableOffset));
        if (!writeIndexTable(file, indexEntries))
        {
            vfLogError("TerrainSerializer: Failed to rewrite index table");
            return false;
        }
        return true;
    }

    bool TerrainSerializer::saveIncremental(const TerrainIncrementalSaveParams& params)
    {
        if (terrainArchiveMode())
        {
            vfLogError("TerrainSerializer: incremental saves are disabled while reading from an archive");
            return false;
        }

        if (!params.dirtyCoords || params.dirtyCoords->empty())
            return true;

        if (!params.grid || !params.currentIndexMap)
            return false;

        fs::path filePath(params.path);
        if (!fs::exists(filePath))
        {
            vfLogError("TerrainSerializer: File not found for incremental save: {}", params.path);
            return false;
        }

        try
        {
            TerrainFormatFlags newFlags = computeFlags(*params.grid, params.physicsConfig, params.streamingConfig);

            if (!validateIncrementalFlags(params.currentHeader.flags, newFlags))
                return false;

            std::fstream file(filePath, std::ios::binary | std::ios::in | std::ios::out);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to open file for incremental save: {}", params.path);
                return false;
            }

            std::vector<TileIndexEntry> indexEntries = buildSortedIndex(*params.currentIndexMap);

            file.seekp(0, std::ios::end);

            if (!writeIncrementalTiles(file, *params.grid, *params.dirtyCoords, newFlags, indexEntries))
                return false;

            TerrainFileHeader updatedHeader = params.currentHeader;
            updatedHeader.flags = newFlags;
            updatedHeader.physicsConfig = params.physicsConfig;
            updatedHeader.streamingConfig = params.streamingConfig;

            if (!writeIncrementalHeaderAndIndex(file, updatedHeader, params.indexTableOffset, indexEntries))
                return false;

            file.flush();
            if (!file.good())
            {
                vfLogError("TerrainSerializer: Failed to flush incremental save");
                return false;
            }

            vfLogInfo("TerrainSerializer: Incremental save: updated {} dirty tiles in {}",
                      params.dirtyCoords->size(), params.path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Incremental save failed for {}: {}", params.path, e.what());
            return false;
        }
    }

    TerrainFileHeader TerrainSerializer::buildSaveHeader(const TerrainSaveParams& params,
                                                         TerrainFormatFlags flags,
                                                         uint32_t tileCount)
    {
        TerrainFileHeader header;
        header.flags = flags;
        header.tileCount = tileCount;
        header.resolution = static_cast<uint8_t>(params.config.resolution);
        header.worldTileSize = params.config.worldTileSize;
        header.maxHeight = params.config.maxHeight;
        header.minHeight = params.config.minHeight;
        header.skirtDepth = params.config.skirtDepth;
        header.gridMinX = params.gridMinX;
        header.gridMinZ = params.gridMinZ;
        header.gridMaxX = params.gridMaxX;
        header.gridMaxZ = params.gridMaxZ;
        header.materialPath = params.materialPath;
        header.physicsConfig = params.physicsConfig;
        header.streamingConfig = params.streamingConfig;
        return header;
    }

    bool TerrainSerializer::writeAllTileData(std::ostream& file,
                                              const std::vector<const TerrainTile*>& tiles,
                                              TerrainFormatFlags flags,
                                              std::vector<TileIndexEntry>& indexEntries)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(tiles.size()); ++i)
        {
            if (!writeTileData(file, *tiles[i], flags, indexEntries[i]))
            {
                vfLogError("TerrainSerializer: Failed to write tile ({}, {})",
                           tiles[i]->coord.x, tiles[i]->coord.z);
                return false;
            }
        }
        return true;
    }

    bool TerrainSerializer::writeFullSaveToStream(std::ostream& file,
                                                    const TerrainFileHeader& header,
                                                    const std::vector<const TerrainTile*>& tiles,
                                                    TerrainFormatFlags flags)
    {
        if (!writeHeader(file, header))
        {
            vfLogError("TerrainSerializer: Failed to write header");
            return false;
        }

        auto indexTablePos = file.tellp();
        if (indexTablePos == std::streampos(-1))
        {
            vfLogError("TerrainSerializer: Failed to get index table position");
            return false;
        }
        std::vector<char> placeholder(header.tileCount * TILE_INDEX_ENTRY_SIZE, 0);
        file.write(placeholder.data(), static_cast<std::streamsize>(placeholder.size()));

        if (!file.good())
        {
            vfLogError("TerrainSerializer: Failed to write index placeholder");
            return false;
        }

        std::vector<TileIndexEntry> indexEntries(header.tileCount);
        if (!writeAllTileData(file, tiles, flags, indexEntries))
            return false;

        file.seekp(indexTablePos);
        if (!writeIndexTable(file, indexEntries))
        {
            vfLogError("TerrainSerializer: Failed to write index table");
            return false;
        }

        file.flush();
        return file.good();
    }

    bool TerrainSerializer::save(const TerrainSaveParams& params)
    {
        if (terrainArchiveMode())
        {
            vfLogError("TerrainSerializer: saves are disabled while reading from an archive");
            return false;
        }

        if (!params.grid)
            return false;

        auto allTiles = params.grid->getAllTiles();
        if (allTiles.empty())
        {
            vfLogWarning("TerrainSerializer: No tiles to save");
            return true;
        }

        std::sort(allTiles.begin(), allTiles.end(),
                  [](const TerrainTile* a, const TerrainTile* b)
                  {
                      if (a->coord.x != b->coord.x) return a->coord.x < b->coord.x;
                      return a->coord.z < b->coord.z;
                  });

        TerrainFormatFlags flags = computeFlags(*params.grid, params.physicsConfig, params.streamingConfig);
        TerrainFileHeader header = buildSaveHeader(params, flags, static_cast<uint32_t>(allTiles.size()));

        try
        {
            fs::path filePath(params.path);
            fs::create_directories(filePath.parent_path());
            fs::path tmpPath = filePath;
            tmpPath += ".tmp";

            std::ofstream file(tmpPath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to create file: {}", params.path);
                return false;
            }

            if (!writeFullSaveToStream(file, header, allTiles, flags))
            {
                vfLogError("TerrainSerializer: Failed to flush file: {}", params.path);
                return false;
            }
            file.close();

            if (fs::exists(filePath))
                fs::remove(filePath);
            fs::rename(tmpPath, filePath);

            vfLogInfo("TerrainSerializer: Saved {} tiles to {}", header.tileCount, params.path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to save {}: {}", params.path, e.what());
            return false;
        }
    }
}
