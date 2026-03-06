#include "TerrainSerializer.hpp"
#include "../print/Log.hpp"
#include "TerrainGrid.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace terrain
{
    namespace fs = std::filesystem;
    using namespace resource::endian;

    bool TerrainSerializer::writeHeader(std::ostream& file, const TerrainFileHeader& header)
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

        uint32_t pathLen = static_cast<uint32_t>(header.materialPath.size());
        writeLE(file, pathLen);
        if (pathLen > 0)
            file.write(header.materialPath.data(), pathLen);

        if (hasFlag(header.flags, TerrainFormatFlags::HAS_PHYSICS_DATA))
        {
            writeLE<uint8_t>(file, header.physicsConfig.hasCollider ? 1 : 0);
            writeLE<uint8_t>(file, header.physicsConfig.collisionLayer);
            writeLE(file, header.physicsConfig.friction);
            writeLE(file, header.physicsConfig.restitution);
        }

        if (hasFlag(header.flags, TerrainFormatFlags::HAS_STREAMING_CONFIG))
        {
            writeLE<uint8_t>(file, header.streamingConfig.enabled ? 1 : 0);
            writeLE(file, header.streamingConfig.loadRadius);
            writeLE(file, header.streamingConfig.unloadRadius);
            writeLE(file, header.streamingConfig.maxLoadsPerFrame);
            writeLE(file, header.streamingConfig.maxUnloadsPerFrame);
        }

        return file.good();
    }

    bool TerrainSerializer::writeIndexTable(std::ostream& file,
                                            const std::vector<TileIndexEntry>& index)
    {
        for (const auto& entry : index)
        {
            writeLE(file, entry.coordX);
            writeLE(file, entry.coordZ);
            writeLE(file, entry.heightDataOffset);
            writeLE(file, entry.heightDataSize);
            writeLE(file, entry.weightDataOffset);
            writeLE(file, entry.meshletDataOffset);
            writeLE(file, entry.holeMaskDataOffset);
        }
        return file.good();
    }

    bool TerrainSerializer::writeTileData(std::ostream& file,
                                          const TerrainTile& tile,
                                          TerrainFormatFlags flags,
                                          TileIndexEntry& outEntry)
    {
        outEntry.coordX = tile.coord.x;
        outEntry.coordZ = tile.coord.z;

        outEntry.heightDataOffset = static_cast<uint64_t>(file.tellp());
        uint32_t heightCount = static_cast<uint32_t>(tile.heightData.size());
        writeLE(file, heightCount);
        writeVectorLE(file, tile.heightData);
        uint64_t afterHeight = static_cast<uint64_t>(file.tellp());
        outEntry.heightDataSize = static_cast<uint32_t>(afterHeight - outEntry.heightDataOffset);

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
                    std::vector<float> zeros(tile.weightMap.getTexelCount(), 0.0f);
                    writeVectorLE(file, zeros);
                }
            }
        }

        outEntry.meshletDataOffset = 0;
        if (hasFlag(flags, TerrainFormatFlags::HAS_MESHLET_CACHE))
        {
            if (!writeTileMeshletData(file, tile, outEntry))
                return false;
        }

        outEntry.holeMaskDataOffset = 0;
        if (hasFlag(flags, TerrainFormatFlags::HAS_HOLE_MASK) && tile.hasHoleMask())
        {
            outEntry.holeMaskDataOffset = static_cast<uint64_t>(file.tellp());

            // Bit-pack the hole mask: ceil(totalVertices / 8) bytes
            size_t totalVertices = tile.holeMask.size();
            uint32_t packedSize = static_cast<uint32_t>((totalVertices + 7) / 8);
            writeLE(file, static_cast<uint32_t>(totalVertices));

            std::vector<uint8_t> packed(packedSize, 0);
            for (size_t i = 0; i < totalVertices; ++i)
            {
                if (tile.holeMask[i])
                    packed[i / 8] |= (1 << (i % 8));
            }
            file.write(reinterpret_cast<const char*>(packed.data()),
                       static_cast<std::streamsize>(packed.size()));
        }

        return file.good();
    }

    bool TerrainSerializer::writeTileMeshletData(std::ostream& file,
                                                  const TerrainTile& tile,
                                                  TileIndexEntry& outEntry)
    {
        bool hasMeshlets = false;
        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            if (tile.lodLevels[lod].hasMeshlets())
            {
                hasMeshlets = true;
                break;
            }
        }

        if (!hasMeshlets)
            return true; // meshletDataOffset stays 0

        outEntry.meshletDataOffset = static_cast<uint64_t>(file.tellp());

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            const auto& lodData = tile.lodLevels[lod];
            writeLE<uint32_t>(file, static_cast<uint32_t>(lodData.meshlets.size()));
            writeLE<uint32_t>(file, static_cast<uint32_t>(lodData.meshletVertices.size()));
            writeLE<uint32_t>(file, static_cast<uint32_t>(lodData.meshletPrimitives.size()));
        }

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            const auto& lodData = tile.lodLevels[lod];
            writeLE<uint32_t>(file, static_cast<uint32_t>(lodData.vertices.size()));

            for (const auto& vertex : lodData.vertices)
            {
                writeLE<float>(file, vertex.position.x);
                writeLE<float>(file, vertex.position.y);
                writeLE<float>(file, vertex.position.z);
                writeLE<float>(file, vertex.normal.x);
                writeLE<float>(file, vertex.normal.y);
                writeLE<float>(file, vertex.normal.z);
                writeLE<float>(file, vertex.texCoords.x);
                writeLE<float>(file, vertex.texCoords.y);
                writeLE<int32_t>(file, vertex.boneIndices.x);
                writeLE<int32_t>(file, vertex.boneIndices.y);
                writeLE<int32_t>(file, vertex.boneIndices.z);
                writeLE<int32_t>(file, vertex.boneIndices.w);
                writeLE<float>(file, vertex.boneWeights.x);
                writeLE<float>(file, vertex.boneWeights.y);
                writeLE<float>(file, vertex.boneWeights.z);
                writeLE<float>(file, vertex.boneWeights.w);
            }
        }

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            const auto& lodData = tile.lodLevels[lod];
            writeLE<uint32_t>(file, static_cast<uint32_t>(lodData.indices.size()));
            writeVectorLE(file, lodData.indices);
        }

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            for (const auto& meshlet : tile.lodLevels[lod].meshlets)
            {
                writeLE<uint32_t>(file, meshlet.descriptor.vertexOffset);
                writeLE<uint32_t>(file, meshlet.descriptor.primitiveOffset);
                writeLE<uint8_t>(file, meshlet.descriptor.vertexCount);
                writeLE<uint8_t>(file, meshlet.descriptor.primitiveCount);
                writeLE<uint16_t>(file, 0); // padding

                writeLE<float>(file, meshlet.bounds.boundingSphere.x);
                writeLE<float>(file, meshlet.bounds.boundingSphere.y);
                writeLE<float>(file, meshlet.bounds.boundingSphere.z);
                writeLE<float>(file, meshlet.bounds.boundingSphere.w);
                writeLE<float>(file, meshlet.bounds.cone.x);
                writeLE<float>(file, meshlet.bounds.cone.y);
                writeLE<float>(file, meshlet.bounds.cone.z);
                writeLE<float>(file, meshlet.bounds.cone.w);
            }
        }

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            writeVectorLE(file, tile.lodLevels[lod].meshletVertices);
        }

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            writeVectorLE(file, tile.lodLevels[lod].meshletPrimitives);
        }

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            const auto& lodData = tile.lodLevels[lod];
            writeLE<float>(file, lodData.aabb.min.x);
            writeLE<float>(file, lodData.aabb.min.y);
            writeLE<float>(file, lodData.aabb.min.z);
            writeLE<float>(file, lodData.aabb.max.x);
            writeLE<float>(file, lodData.aabb.max.y);
            writeLE<float>(file, lodData.aabb.max.z);
            writeLE<float>(file, lodData.boundingSphere.x);
            writeLE<float>(file, lodData.boundingSphere.y);
            writeLE<float>(file, lodData.boundingSphere.z);
            writeLE<float>(file, lodData.boundingSphere.w);
            writeLE<float>(file, lodData.geometricError);
        }

        return file.good();
    }

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

    TerrainFormatFlags TerrainSerializer::computeFlags(const TerrainGrid& grid,
                                                       const TerrainPhysicsConfig& physicsConfig,
                                                       const TerrainStreamingConfig& streamingConfig)
    {
        TerrainFormatFlags flags = TerrainFormatFlags::NONE;

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

        return flags;
    }

    bool TerrainSerializer::saveIncremental(
        std::string_view path,
        const TerrainGrid& grid,
        const std::unordered_set<TileCoord, TileCoordHash>& dirtyCoords,
        const TerrainFileHeader& currentHeader,
        uint64_t indexTableOffset,
        const std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash>& currentIndexMap,
        const TerrainPhysicsConfig& physicsConfig,
        const TerrainStreamingConfig& streamingConfig)
    {
        if (dirtyCoords.empty())
            return true;

        fs::path filePath(path);
        if (!fs::exists(filePath))
        {
            vfLogError("TerrainSerializer: File not found for incremental save: {}", path);
            return false;
        }

        try
        {
            TerrainFormatFlags newFlags = computeFlags(grid, physicsConfig, streamingConfig);

            // If optional header sections toggled, header size changed — fall back to full save
            bool hadPhysics = hasFlag(currentHeader.flags, TerrainFormatFlags::HAS_PHYSICS_DATA);
            bool hasPhysicsNow = hasFlag(newFlags, TerrainFormatFlags::HAS_PHYSICS_DATA);
            bool hadStreaming = hasFlag(currentHeader.flags, TerrainFormatFlags::HAS_STREAMING_CONFIG);
            bool hasStreamingNow = hasFlag(newFlags, TerrainFormatFlags::HAS_STREAMING_CONFIG);

            if (hadPhysics != hasPhysicsNow || hadStreaming != hasStreamingNow)
            {
                vfLogWarning("TerrainSerializer: Header size changed, falling back to full save");
                return false;
            }

            std::fstream file(filePath, std::ios::binary | std::ios::in | std::ios::out);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to open file for incremental save: {}", path);
                return false;
            }

            // Build sorted index from current map
            std::vector<TileIndexEntry> indexEntries;
            indexEntries.reserve(currentIndexMap.size());
            for (const auto& [coord, entry] : currentIndexMap)
                indexEntries.push_back(entry);

            std::sort(indexEntries.begin(), indexEntries.end(),
                [](const TileIndexEntry& a, const TileIndexEntry& b) {
                    if (a.coordX != b.coordX) return a.coordX < b.coordX;
                    return a.coordZ < b.coordZ;
                });

            // Seek to end of file for appending dirty tile data
            file.seekp(0, std::ios::end);

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
                if (!writeTileData(file, *tile, newFlags, newEntry))
                {
                    vfLogError("TerrainSerializer: Failed to write dirty tile ({}, {})",
                               coord.x, coord.z);
                    return false;
                }

                // Update the matching entry in the sorted index
                for (auto& entry : indexEntries)
                {
                    if (entry.coordX == coord.x && entry.coordZ == coord.z)
                    {
                        entry = newEntry;
                        break;
                    }
                }
            }

            // Rewrite header in-place (flags may have changed, e.g., HAS_HOLE_MASK added)
            file.seekp(0, std::ios::beg);
            TerrainFileHeader updatedHeader = currentHeader;
            updatedHeader.flags = newFlags;
            updatedHeader.physicsConfig = physicsConfig;
            updatedHeader.streamingConfig = streamingConfig;
            if (!writeHeader(file, updatedHeader))
            {
                vfLogError("TerrainSerializer: Failed to rewrite header");
                return false;
            }

            // Rewrite index table in-place (same tile count, same position)
            file.seekp(static_cast<std::streamoff>(indexTableOffset));
            if (!writeIndexTable(file, indexEntries))
            {
                vfLogError("TerrainSerializer: Failed to rewrite index table");
                return false;
            }

            file.flush();
            if (!file.good())
            {
                vfLogError("TerrainSerializer: Failed to flush incremental save");
                return false;
            }

            vfLogInfo("TerrainSerializer: Incremental save: updated {} dirty tiles in {}",
                      dirtyCoords.size(), path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Incremental save failed for {}: {}", path, e.what());
            return false;
        }
    }

    bool TerrainSerializer::save(
        std::string_view path,
        const TerrainGrid& grid,
        const TerrainTileConfig& config,
        int32_t gridMinX, int32_t gridMinZ,
        int32_t gridMaxX, int32_t gridMaxZ,
        const std::string& materialPath,
        const TerrainPhysicsConfig& physicsConfig,
        const TerrainStreamingConfig& streamingConfig)
    {
        auto allTiles = grid.getAllTiles();
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

        TerrainFormatFlags flags = computeFlags(grid, physicsConfig, streamingConfig);

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
        header.physicsConfig = physicsConfig;
        header.streamingConfig = streamingConfig;

        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            fs::path tmpPath = filePath;
            tmpPath += ".tmp";

            std::ofstream file(tmpPath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to create file: {}", path);
                return false;
            }

            if (!writeHeader(file, header))
            {
                vfLogError("TerrainSerializer: Failed to write header");
                return false;
            }

            auto indexTablePos = file.tellp();
            constexpr size_t INDEX_ENTRY_SIZE = 44; // 4+4+8+4+8+8+8
            std::vector<char> placeholder(header.tileCount * INDEX_ENTRY_SIZE, 0);
            file.write(placeholder.data(), static_cast<std::streamsize>(placeholder.size()));

            if (!file.good())
            {
                vfLogError("TerrainSerializer: Failed to write index placeholder");
                return false;
            }

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
}
