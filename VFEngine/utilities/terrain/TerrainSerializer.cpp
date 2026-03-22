#include "TerrainSerializer.hpp"
#include "TerrainCompression.hpp"
#include "../print/Log.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <filesystem>

namespace terrain
{
    namespace fs = std::filesystem;
    using namespace resource::endian;

    static bool safeTellp(std::ostream& file, uint64_t& outPos)
    {
        auto pos = file.tellp();
        if (pos == std::streampos(-1) || !file.good())
            return false;
        outPos = static_cast<uint64_t>(pos);
        return true;
    }

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
            writeLE(file, entry.caveSdfDataOffset);
        }
        return file.good();
    }

    static bool writeTileHeightData(std::ostream& file, const TerrainTile& tile,
                                     TileIndexEntry& outEntry)
    {
        if (!safeTellp(file, outEntry.heightDataOffset))
            return false;
        uint32_t heightCount = static_cast<uint32_t>(tile.heightData.size());
        writeLE(file, heightCount);

        auto heightParams = compression::computeHeightRange(tile.heightData);
        writeLE(file, heightParams.minH);
        writeLE(file, heightParams.maxH);
        auto quantizedHeights = compression::quantizeHeights(tile.heightData, heightParams);
        writeVectorLE(file, quantizedHeights);

        uint64_t afterHeight = 0;
        if (!safeTellp(file, afterHeight))
            return false;
        outEntry.heightDataSize = static_cast<uint32_t>(afterHeight - outEntry.heightDataOffset);
        return true;
    }

    static bool writeTileWeightData(std::ostream& file, const TerrainTile& tile,
                                     TileIndexEntry& outEntry)
    {
        if (!safeTellp(file, outEntry.weightDataOffset))
            return false;

        for (uint8_t i = 0; i < WEIGHT_CHANNELS; ++i)
            writeLE<uint8_t>(file, tile.weightMap.layerIndices[i]);

        writeLE(file, tile.weightMap.resolution);

        for (uint8_t ch = 0; ch < WEIGHT_CHANNELS; ++ch)
        {
            if (ch < tile.weightMap.layerWeights.size())
            {
                auto quantizedWeights = compression::quantizeWeights(tile.weightMap.layerWeights[ch]);
                file.write(reinterpret_cast<const char*>(quantizedWeights.data()),
                           static_cast<std::streamsize>(quantizedWeights.size()));
            }
            else
            {
                std::vector<uint8_t> zeros(tile.weightMap.getTexelCount(), 0);
                file.write(reinterpret_cast<const char*>(zeros.data()),
                           static_cast<std::streamsize>(zeros.size()));
            }
        }
        return true;
    }

    static bool writeTileHoleMaskData(std::ostream& file, const TerrainTile& tile,
                                       TileIndexEntry& outEntry)
    {
        if (!safeTellp(file, outEntry.holeMaskDataOffset))
            return false;

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
        return true;
    }

    bool TerrainSerializer::writeTileData(std::ostream& file,
                                          const TerrainTile& tile,
                                          TerrainFormatFlags flags,
                                          TileIndexEntry& outEntry)
    {
        outEntry.coordX = tile.coord.x;
        outEntry.coordZ = tile.coord.z;

        if (!writeTileHeightData(file, tile, outEntry))
            return false;

        outEntry.weightDataOffset = 0;
        if (hasFlag(flags, TerrainFormatFlags::HAS_WEIGHT_MAPS) && tile.weightMap.isInitialized())
        {
            if (!writeTileWeightData(file, tile, outEntry))
                return false;
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
            if (!writeTileHoleMaskData(file, tile, outEntry))
                return false;
        }

        outEntry.caveSdfDataOffset = 0;
        if (hasFlag(flags, TerrainFormatFlags::HAS_CAVE_DATA) && tile.hasCaveData())
        {
            if (!writeTileCaveData(file, tile, outEntry))
                return false;
        }

        return file.good();
    }

    static void writeVertex(std::ostream& file, const resource::Vertex& vertex)
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

    static void writeMeshletDescriptor(std::ostream& file, const resource::Meshlet& meshlet)
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

    static void writeLODBounds(std::ostream& file, const TileLODData& lodData)
    {
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

    static bool tileHasMeshlets(const TerrainTile& tile)
    {
        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            if (tile.lodLevels[lod].hasMeshlets())
                return true;
        }
        return false;
    }

    static void writeMeshletHeaders(std::ostream& file, const TerrainTile& tile)
    {
        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            const auto& d = tile.lodLevels[lod];
            writeLE<uint32_t>(file, static_cast<uint32_t>(d.meshlets.size()));
            writeLE<uint32_t>(file, static_cast<uint32_t>(d.meshletVertices.size()));
            writeLE<uint32_t>(file, static_cast<uint32_t>(d.meshletPrimitives.size()));
        }
    }

    static void writeMeshletGeometry(std::ostream& file, const TerrainTile& tile)
    {
        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            const auto& d = tile.lodLevels[lod];
            writeLE<uint32_t>(file, static_cast<uint32_t>(d.vertices.size()));
            for (const auto& v : d.vertices)
                writeVertex(file, v);
        }

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            const auto& d = tile.lodLevels[lod];
            writeLE<uint32_t>(file, static_cast<uint32_t>(d.indices.size()));
            writeVectorLE(file, d.indices);
        }

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
            for (const auto& m : tile.lodLevels[lod].meshlets)
                writeMeshletDescriptor(file, m);

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
            writeVectorLE(file, tile.lodLevels[lod].meshletVertices);

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
            writeVectorLE(file, tile.lodLevels[lod].meshletPrimitives);

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
            writeLODBounds(file, tile.lodLevels[lod]);
    }

    bool TerrainSerializer::writeTileMeshletData(std::ostream& file,
                                                  const TerrainTile& tile,
                                                  TileIndexEntry& outEntry)
    {
        if (!tileHasMeshlets(tile))
            return true;

        if (!safeTellp(file, outEntry.meshletDataOffset))
            return false;

        writeMeshletHeaders(file, tile);
        writeMeshletGeometry(file, tile);

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

            compression::HeightQuantizationParams params;
            params.minH = readLE<float>(file);
            params.maxH = readLE<float>(file);
            std::vector<uint16_t> quantized;
            readVectorLE(file, quantized, heightCount);
            outHeights = compression::dequantizeHeights(quantized, params);

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

    bool TerrainSerializer::writeTileCaveData(std::ostream& file,
                                               const TerrainTile& tile,
                                               TileIndexEntry& outEntry)
    {
        if (!tile.hasCaveData())
            return true;

        const auto& sdf = *tile.caveData;

        uint64_t pos;
        if (!safeTellp(file, pos))
            return false;
        outEntry.caveSdfDataOffset = pos;

        // Write SDF config
        writeLE<uint32_t>(file, sdf.config.resX);
        writeLE<uint32_t>(file, sdf.config.resY);
        writeLE<uint32_t>(file, sdf.config.resZ);
        writeLE<float>(file, sdf.config.voxelSize);
        writeLE<float>(file, sdf.config.yVoxelSize);
        writeLE<float>(file, sdf.config.yExtentBelow);
        writeLE<float>(file, 0.0f); // reserved

        // Write local origin
        writeLE<float>(file, sdf.localOrigin.x);
        writeLE<float>(file, sdf.localOrigin.y);
        writeLE<float>(file, sdf.localOrigin.z);

        // Write SDF grid data (raw floats)
        uint32_t gridSize = static_cast<uint32_t>(sdf.sdfGrid.size());
        writeLE<uint32_t>(file, gridSize);

        for (uint32_t i = 0; i < gridSize; ++i)
        {
            writeLE<float>(file, sdf.sdfGrid[i]);
        }

        // Write original SDF grid (for detecting carved regions)
        uint32_t hasOriginal = sdf.originalSdfGrid.empty() ? 0u : 1u;
        writeLE<uint32_t>(file, hasOriginal);
        if (hasOriginal)
        {
            for (uint32_t i = 0; i < gridSize; ++i)
            {
                writeLE<float>(file, sdf.originalSdfGrid[i]);
            }
        }

        return file.good();
    }

    bool TerrainSerializer::readTileCaveData(std::string_view path,
                                              const TileIndexEntry& entry,
                                              CaveSDFData& outCaveData)
    {
        if (entry.caveSdfDataOffset == 0)
            return false;

        try
        {
            std::ifstream file(std::string(path), std::ios::binary);
            if (!file.is_open())
                return false;

            file.seekg(static_cast<std::streamoff>(entry.caveSdfDataOffset));

            // Read SDF config
            outCaveData.config.resX = readLE<uint32_t>(file);
            outCaveData.config.resY = readLE<uint32_t>(file);
            outCaveData.config.resZ = readLE<uint32_t>(file);
            outCaveData.config.voxelSize = readLE<float>(file);
            outCaveData.config.yVoxelSize = readLE<float>(file);
            outCaveData.config.yExtentBelow = readLE<float>(file);
            readLE<float>(file); // reserved

            // Read local origin
            outCaveData.localOrigin.x = readLE<float>(file);
            outCaveData.localOrigin.y = readLE<float>(file);
            outCaveData.localOrigin.z = readLE<float>(file);

            // Read SDF grid data (raw floats)
            uint32_t gridSize = readLE<uint32_t>(file);
            outCaveData.sdfGrid.resize(gridSize);

            for (uint32_t i = 0; i < gridSize; ++i)
            {
                outCaveData.sdfGrid[i] = readLE<float>(file);
            }

            // Read original SDF grid
            uint32_t hasOriginal = readLE<uint32_t>(file);
            if (hasOriginal)
            {
                outCaveData.originalSdfGrid.resize(gridSize);
                for (uint32_t i = 0; i < gridSize; ++i)
                {
                    outCaveData.originalSdfGrid[i] = readLE<float>(file);
                }
            }

            return file.good();
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to read cave data for tile ({}, {}): {}",
                       entry.coordX, entry.coordZ, e.what());
            return false;
        }
    }

}
