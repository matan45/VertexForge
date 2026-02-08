#include "TerrainSerializer.hpp"
#include "TerrainGrid.hpp"
#include "../print/EditorLogger.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace terrain
{
    namespace fs = std::filesystem;
    using namespace resource::endian;

    // ── helpers ──────────────────────────────────────────────────────────

    static bool validateResolution(uint8_t res)
    {
        return res <= static_cast<uint8_t>(TileResolution::High);
    }

    static uint32_t resolutionToVertexCount(uint8_t res)
    {
        return TILE_VERTEX_COUNTS[res];
    }

    // ── writeHeader ─────────────────────────────────────────────────────

    bool TerrainSerializer::writeHeader(std::ofstream& file, const TerrainFileHeader& header)
    {
        file.write(TERRAIN_MAGIC.data(), 4);

        writeLE(file, header.versionMajor);
        writeLE(file, header.versionMinor);
        writeLE(file, header.versionPatch);
        writeLE(file, static_cast<uint32_t>(header.flags));

        writeLE(file, header.tileCount);
        writeLE(file, header.resolution);
        writeLE(file, header.worldTileSize);
        writeLE(file, header.maxHeight);
        writeLE(file, header.minHeight);
        writeLE(file, header.skirtDepth);

        for (uint32_t i = 0; i < TERRAIN_LOD_COUNT; ++i)
            writeLE(file, header.lodDistances[i]);

        writeLE(file, header.gridMinX);
        writeLE(file, header.gridMinZ);
        writeLE(file, header.gridMaxX);
        writeLE(file, header.gridMaxZ);

        // Material path (length-prefixed string)
        uint32_t pathLen = static_cast<uint32_t>(header.materialPath.size());
        writeLE(file, pathLen);
        if (pathLen > 0)
            file.write(header.materialPath.data(), pathLen);

        return file.good();
    }

    // ── writeIndexTable ─────────────────────────────────────────────────

    bool TerrainSerializer::writeIndexTable(std::ofstream& file,
                                            const std::vector<TileIndexEntry>& index)
    {
        for (const auto& entry : index)
        {
            writeLE(file, entry.coordX);
            writeLE(file, entry.coordZ);
            writeLE(file, entry.heightDataOffset);
            writeLE(file, entry.heightDataSize);
            writeLE(file, entry.weightDataOffset);
        }
        return file.good();
    }

    // ── writeTileData ───────────────────────────────────────────────────

    bool TerrainSerializer::writeTileData(std::ofstream& file,
                                          const TerrainTile& tile,
                                          TerrainFormatFlags flags,
                                          TileIndexEntry& outEntry)
    {
        outEntry.coordX = tile.coord.x;
        outEntry.coordZ = tile.coord.z;

        // Height data section
        outEntry.heightDataOffset = static_cast<uint64_t>(file.tellp());
        uint32_t heightCount = static_cast<uint32_t>(tile.heightData.size());
        writeLE(file, heightCount);
        writeVectorLE(file, tile.heightData);
        uint64_t afterHeight = static_cast<uint64_t>(file.tellp());
        outEntry.heightDataSize = static_cast<uint32_t>(afterHeight - outEntry.heightDataOffset);

        // Weight map section (optional)
        outEntry.weightDataOffset = 0;
        if (hasFlag(flags, TerrainFormatFlags::HAS_WEIGHT_MAPS) && tile.weightMap.isInitialized())
        {
            outEntry.weightDataOffset = static_cast<uint64_t>(file.tellp());
            writeLE(file, tile.weightMap.activeLayerCount);
            writeLE(file, tile.weightMap.resolution);

            for (uint8_t layer = 0; layer < tile.weightMap.activeLayerCount; ++layer)
            {
                if (layer < tile.weightMap.layerWeights.size())
                {
                    writeVectorLE(file, tile.weightMap.layerWeights[layer]);
                }
                else
                {
                    // Pad with zeros for missing layers
                    std::vector<float> zeros(tile.weightMap.getTexelCount(), 0.0f);
                    writeVectorLE(file, zeros);
                }
            }
        }

        return file.good();
    }

    // ── save ────────────────────────────────────────────────────────────

    bool TerrainSerializer::save(
        std::string_view path,
        const TerrainGrid& grid,
        const TerrainTileConfig& config,
        int32_t gridMinX, int32_t gridMinZ,
        int32_t gridMaxX, int32_t gridMaxZ,
        const std::string& materialPath)
    {
        auto allTiles = grid.getAllTiles();
        if (allTiles.empty())
        {
            vfLogWarning("TerrainSerializer: No tiles to save");
            return true;
        }

        // Sort tiles by coordinate for deterministic output
        std::sort(allTiles.begin(), allTiles.end(),
                  [](const TerrainTile* a, const TerrainTile* b)
                  {
                      if (a->coord.x != b->coord.x) return a->coord.x < b->coord.x;
                      return a->coord.z < b->coord.z;
                  });

        // Determine flags
        TerrainFormatFlags flags = TerrainFormatFlags::NONE;
        for (const auto* tile : allTiles)
        {
            if (tile->weightMap.isInitialized())
            {
                flags = flags | TerrainFormatFlags::HAS_WEIGHT_MAPS;
                break;
            }
        }

        // Build header
        TerrainFileHeader header;
        header.flags = flags;
        header.tileCount = static_cast<uint32_t>(allTiles.size());
        header.resolution = static_cast<uint8_t>(config.resolution);
        header.worldTileSize = config.worldTileSize;
        header.maxHeight = config.maxHeight;
        header.minHeight = config.minHeight;
        header.skirtDepth = config.skirtDepth;
        header.lodDistances = config.lodDistances;
        header.gridMinX = gridMinX;
        header.gridMinZ = gridMinZ;
        header.gridMaxX = gridMaxX;
        header.gridMaxZ = gridMaxZ;
        header.materialPath = materialPath;

        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            // Write to temp file for atomic replacement
            fs::path tmpPath = filePath;
            tmpPath += ".tmp";

            std::ofstream file(tmpPath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to create file: {}", path);
                return false;
            }

            // 1. Write header
            if (!writeHeader(file, header))
            {
                vfLogError("TerrainSerializer: Failed to write header");
                return false;
            }

            // 2. Record index table position, write placeholder zeros
            auto indexTablePos = file.tellp();
            constexpr size_t INDEX_ENTRY_SIZE = 28; // 4+4+8+4+8
            std::vector<char> placeholder(header.tileCount * INDEX_ENTRY_SIZE, 0);
            file.write(placeholder.data(), static_cast<std::streamsize>(placeholder.size()));

            if (!file.good())
            {
                vfLogError("TerrainSerializer: Failed to write index placeholder");
                return false;
            }

            // 3. Write tile data, recording offsets
            std::vector<TileIndexEntry> indexEntries(header.tileCount);
            for (uint32_t i = 0; i < header.tileCount; ++i)
            {
                if (!writeTileData(file, *allTiles[i], flags, indexEntries[i]))
                {
                    vfLogError("TerrainSerializer: Failed to write tile ({}, {})",
                               allTiles[i]->coord.x, allTiles[i]->coord.z);
                    return false;
                }
            }

            // 4. Seek back and write the real index table
            file.seekp(indexTablePos);
            if (!writeIndexTable(file, indexEntries))
            {
                vfLogError("TerrainSerializer: Failed to write index table");
                return false;
            }

            file.flush();
            if (!file.good())
            {
                vfLogError("TerrainSerializer: Failed to flush file: {}", path);
                return false;
            }
            file.close();

            // Atomic replace
            if (fs::exists(filePath))
                fs::remove(filePath);
            fs::rename(tmpPath, filePath);

            vfLogInfo("TerrainSerializer: Saved {} tiles to {}", header.tileCount, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to save {}: {}", path, e.what());
            return false;
        }
    }

    // ── parseHeader ─────────────────────────────────────────────────────

    bool TerrainSerializer::parseHeader(std::ifstream& file, TerrainFileHeader& outHeader)
    {
        // Magic bytes
        std::array<char, 4> magic{};
        file.read(magic.data(), 4);
        if (magic != TERRAIN_MAGIC)
        {
            vfLogError("TerrainSerializer: Invalid magic bytes");
            return false;
        }

        outHeader.versionMajor = readLE<uint32_t>(file);
        outHeader.versionMinor = readLE<uint32_t>(file);
        outHeader.versionPatch = readLE<uint32_t>(file);

        if (outHeader.versionMajor > TERRAIN_FORMAT_VERSION_MAJOR)
        {
            vfLogError("TerrainSerializer: Unsupported version {}.{}.{}",
                       outHeader.versionMajor, outHeader.versionMinor, outHeader.versionPatch);
            return false;
        }

        outHeader.flags = static_cast<TerrainFormatFlags>(readLE<uint32_t>(file));

        outHeader.tileCount = readLE<uint32_t>(file);
        if (outHeader.tileCount > MAX_REASONABLE_TERRAIN_TILES)
        {
            vfLogError("TerrainSerializer: Unreasonable tile count {}", outHeader.tileCount);
            return false;
        }

        outHeader.resolution = readLE<uint8_t>(file);
        if (!validateResolution(outHeader.resolution))
        {
            vfLogError("TerrainSerializer: Invalid resolution {}", outHeader.resolution);
            return false;
        }

        outHeader.worldTileSize = readLE<float>(file);
        outHeader.maxHeight = readLE<float>(file);
        outHeader.minHeight = readLE<float>(file);
        outHeader.skirtDepth = readLE<float>(file);

        for (uint32_t i = 0; i < TERRAIN_LOD_COUNT; ++i)
            outHeader.lodDistances[i] = readLE<float>(file);

        outHeader.gridMinX = readLE<int32_t>(file);
        outHeader.gridMinZ = readLE<int32_t>(file);
        outHeader.gridMaxX = readLE<int32_t>(file);
        outHeader.gridMaxZ = readLE<int32_t>(file);

        // Material path
        uint32_t pathLen = readLE<uint32_t>(file);
        if (pathLen > 0)
        {
            outHeader.materialPath.resize(pathLen);
            file.read(outHeader.materialPath.data(), pathLen);
        }

        return file.good();
    }

    // ── parseIndexTable ─────────────────────────────────────────────────

    bool TerrainSerializer::parseIndexTable(std::ifstream& file, uint32_t tileCount,
                                            std::vector<TileIndexEntry>& outIndex)
    {
        outIndex.resize(tileCount);
        for (uint32_t i = 0; i < tileCount; ++i)
        {
            outIndex[i].coordX = readLE<int32_t>(file);
            outIndex[i].coordZ = readLE<int32_t>(file);
            outIndex[i].heightDataOffset = readLE<uint64_t>(file);
            outIndex[i].heightDataSize = readLE<uint32_t>(file);
            outIndex[i].weightDataOffset = readLE<uint64_t>(file);
        }
        return file.good();
    }

    // ── loadAll ─────────────────────────────────────────────────────────

    bool TerrainSerializer::loadAll(
        std::string_view path,
        TerrainFileHeader& outHeader,
        std::vector<TileLoadResult>& outTiles)
    {
        fs::path filePath(path);
        if (!fs::exists(filePath))
        {
            vfLogError("TerrainSerializer: File not found: {}", path);
            return false;
        }

        try
        {
            std::ifstream file(filePath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to open file: {}", path);
                return false;
            }

            if (!parseHeader(file, outHeader))
                return false;

            std::vector<TileIndexEntry> index;
            if (!parseIndexTable(file, outHeader.tileCount, index))
            {
                vfLogError("TerrainSerializer: Failed to read index table");
                return false;
            }

            uint32_t expectedHeightCount = resolutionToVertexCount(outHeader.resolution);
            expectedHeightCount *= expectedHeightCount;

            outTiles.resize(outHeader.tileCount);
            for (uint32_t i = 0; i < outHeader.tileCount; ++i)
            {
                auto& result = outTiles[i];
                const auto& entry = index[i];
                result.coord = TileCoord(entry.coordX, entry.coordZ);

                // Seek to height data
                file.seekg(static_cast<std::streamoff>(entry.heightDataOffset));
                uint32_t heightCount = readLE<uint32_t>(file);
                if (heightCount != expectedHeightCount)
                {
                    vfLogError("TerrainSerializer: Height count mismatch for tile ({}, {}): got {}, expected {}",
                               entry.coordX, entry.coordZ, heightCount, expectedHeightCount);
                    return false;
                }
                readVectorLE(file, result.heightData, heightCount);

                // Weight map (optional)
                if (hasFlag(outHeader.flags, TerrainFormatFlags::HAS_WEIGHT_MAPS)
                    && entry.weightDataOffset != 0)
                {
                    file.seekg(static_cast<std::streamoff>(entry.weightDataOffset));
                    result.weightMap.activeLayerCount = readLE<uint8_t>(file);
                    result.weightMap.resolution = readLE<uint32_t>(file);

                    if (result.weightMap.activeLayerCount == 0 ||
                        result.weightMap.activeLayerCount > MAX_TERRAIN_LAYERS)
                    {
                        vfLogWarning("TerrainSerializer: Invalid layer count {} for tile ({}, {}), skipping weights",
                                     result.weightMap.activeLayerCount, entry.coordX, entry.coordZ);
                        result.weightMap = TileWeightMapData{};
                    }
                    else
                    {
                        size_t texelCount = static_cast<size_t>(result.weightMap.resolution)
                                            * result.weightMap.resolution;
                        result.weightMap.layerWeights.resize(result.weightMap.activeLayerCount);
                        for (uint8_t layer = 0; layer < result.weightMap.activeLayerCount; ++layer)
                        {
                            readVectorLE(file, result.weightMap.layerWeights[layer], texelCount);
                        }
                    }
                }

                if (!file.good())
                {
                    vfLogError("TerrainSerializer: Read error at tile ({}, {})", entry.coordX, entry.coordZ);
                    return false;
                }

                result.success = true;
            }

            vfLogInfo("TerrainSerializer: Loaded {} tiles from {}", outHeader.tileCount, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to load {}: {}", path, e.what());
            return false;
        }
    }

    // ── readHeader (streaming: header + index only) ─────────────────────

    bool TerrainSerializer::readHeader(
        std::string_view path,
        TerrainFileHeader& outHeader,
        std::vector<TileIndexEntry>& outIndex)
    {
        fs::path filePath(path);
        if (!fs::exists(filePath))
        {
            vfLogError("TerrainSerializer: File not found: {}", path);
            return false;
        }

        try
        {
            std::ifstream file(filePath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to open file: {}", path);
                return false;
            }

            if (!parseHeader(file, outHeader))
                return false;

            if (!parseIndexTable(file, outHeader.tileCount, outIndex))
            {
                vfLogError("TerrainSerializer: Failed to read index table");
                return false;
            }

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to read header from {}: {}", path, e.what());
            return false;
        }
    }

    // ── readTileHeights (streaming: single tile) ────────────────────────

    bool TerrainSerializer::readTileHeights(
        std::string_view path,
        const TileIndexEntry& entry,
        std::vector<float>& outHeights)
    {
        if (entry.heightDataOffset == 0)
        {
            vfLogError("TerrainSerializer: Invalid height data offset for tile ({}, {})",
                       entry.coordX, entry.coordZ);
            return false;
        }

        try
        {
            std::ifstream file(fs::path(path), std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to open file: {}", path);
                return false;
            }

            file.seekg(static_cast<std::streamoff>(entry.heightDataOffset));
            uint32_t heightCount = readLE<uint32_t>(file);
            readVectorLE(file, outHeights, heightCount);

            if (!file.good())
            {
                vfLogError("TerrainSerializer: Read error for tile ({}, {})", entry.coordX, entry.coordZ);
                return false;
            }

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to read heights for tile ({}, {}): {}",
                       entry.coordX, entry.coordZ, e.what());
            return false;
        }
    }

    // ── readTileWeights (streaming: single tile) ────────────────────────

    bool TerrainSerializer::readTileWeights(
        std::string_view path,
        const TileIndexEntry& entry,
        TileWeightMapData& outWeights)
    {
        if (entry.weightDataOffset == 0)
        {
            // No weight data for this tile
            outWeights = TileWeightMapData{};
            return true;
        }

        try
        {
            std::ifstream file(fs::path(path), std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to open file: {}", path);
                return false;
            }

            file.seekg(static_cast<std::streamoff>(entry.weightDataOffset));
            outWeights.activeLayerCount = readLE<uint8_t>(file);
            outWeights.resolution = readLE<uint32_t>(file);

            if (outWeights.activeLayerCount == 0 || outWeights.activeLayerCount > MAX_TERRAIN_LAYERS)
            {
                vfLogError("TerrainSerializer: Invalid layer count {} for tile ({}, {})",
                           outWeights.activeLayerCount, entry.coordX, entry.coordZ);
                outWeights = TileWeightMapData{};
                return false;
            }

            size_t texelCount = static_cast<size_t>(outWeights.resolution) * outWeights.resolution;
            outWeights.layerWeights.resize(outWeights.activeLayerCount);
            for (uint8_t layer = 0; layer < outWeights.activeLayerCount; ++layer)
            {
                readVectorLE(file, outWeights.layerWeights[layer], texelCount);
            }

            if (!file.good())
            {
                vfLogError("TerrainSerializer: Read error for tile ({}, {}) weights",
                           entry.coordX, entry.coordZ);
                return false;
            }

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to read weights for tile ({}, {}): {}",
                       entry.coordX, entry.coordZ, e.what());
            return false;
        }
    }
}
