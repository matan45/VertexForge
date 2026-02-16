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

        return file.good();
    }

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
            writeLE(file, entry.meshletDataOffset);
        }
        return file.good();
    }

    bool TerrainSerializer::writeTileData(std::ofstream& file,
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

        return file.good();
    }

    bool TerrainSerializer::writeTileMeshletData(std::ofstream& file,
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

    bool TerrainSerializer::save(
        std::string_view path,
        const TerrainGrid& grid,
        const TerrainTileConfig& config,
        int32_t gridMinX, int32_t gridMinZ,
        int32_t gridMaxX, int32_t gridMaxZ,
        const std::string& materialPath,
        const TerrainPhysicsConfig& physicsConfig)
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

        TerrainFormatFlags flags = TerrainFormatFlags::NONE;
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
        {
            flags = flags | TerrainFormatFlags::HAS_PHYSICS_DATA;
        }

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
            constexpr size_t INDEX_ENTRY_SIZE = 36; // 4+4+8+4+8+8
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
