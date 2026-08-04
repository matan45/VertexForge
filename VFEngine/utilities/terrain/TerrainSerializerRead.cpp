#include "TerrainSerializer.hpp"
#include "TerrainCompression.hpp"
#include "TerrainFileStream.hpp"
#include "../print/Log.hpp"
#include "TerrainGrid.hpp"
#include "../resource/EndianUtils.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#include <shared_mutex>
#include <utility>

namespace terrain
{
    using namespace resource::endian;

    static constexpr uint32_t MAX_PATH_LENGTH = 4096;
    static constexpr uint32_t MAX_TILE_COUNT = 100000;
    static constexpr uint32_t MAX_VERTICES_PER_LOD = 1 << 20;   // ~1M vertices
    static constexpr uint32_t MAX_INDICES_PER_LOD = MAX_VERTICES_PER_LOD * 6;
    static constexpr uint32_t MAX_MESHLETS_PER_LOD = MAX_VERTICES_PER_LOD;
    static bool validateResolution(uint8_t res)
    {
        return res <= static_cast<uint8_t>(TileResolution::High);
    }

    bool TerrainSerializer::parseHeader(std::istream& file, TerrainFileHeader& outHeader)
    {
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

        if (outHeader.versionMajor != TERRAIN_FORMAT_VERSION_MAJOR ||
            outHeader.versionMinor != TERRAIN_FORMAT_VERSION_MINOR ||
            outHeader.versionPatch != TERRAIN_FORMAT_VERSION_PATCH)
        {
            vfLogError("TerrainSerializer: Incompatible version {}.{}.{}, expected {}.{}.{}. Re-import required.",
                       outHeader.versionMajor, outHeader.versionMinor, outHeader.versionPatch,
                       TERRAIN_FORMAT_VERSION_MAJOR, TERRAIN_FORMAT_VERSION_MINOR, TERRAIN_FORMAT_VERSION_PATCH);
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

        uint32_t pathLen = readLE<uint32_t>(file);
        if (pathLen > MAX_PATH_LENGTH)
        {
            vfLogError("TerrainSerializer: Material path length {} exceeds maximum {}", pathLen, MAX_PATH_LENGTH);
            return false;
        }
        if (pathLen > 0)
        {
            outHeader.materialPath.resize(pathLen);
            file.read(outHeader.materialPath.data(), pathLen);
        }

        if (hasFlag(outHeader.flags, TerrainFormatFlags::HAS_PHYSICS_DATA))
        {
            outHeader.physicsConfig.hasCollider = readLE<uint8_t>(file) != 0;
            outHeader.physicsConfig.collisionLayer = readLE<uint8_t>(file);
            outHeader.physicsConfig.friction = readLE<float>(file);
            outHeader.physicsConfig.restitution = readLE<float>(file);
        }

        if (hasFlag(outHeader.flags, TerrainFormatFlags::HAS_STREAMING_CONFIG))
        {
            outHeader.streamingConfig.enabled = readLE<uint8_t>(file) != 0;
            outHeader.streamingConfig.loadRadius = readLE<float>(file);
            outHeader.streamingConfig.unloadRadius = readLE<float>(file);
            outHeader.streamingConfig.maxLoadsPerFrame = readLE<int32_t>(file);
            outHeader.streamingConfig.maxUnloadsPerFrame = readLE<int32_t>(file);
        }

        return file.good();
    }

    bool TerrainSerializer::parseIndexTable(std::istream& file, uint32_t tileCount,
                                            std::vector<TileIndexEntry>& outIndex)
    {
        if (tileCount > MAX_TILE_COUNT)
        {
            vfLogError("TerrainSerializer: Tile count {} exceeds maximum {}", tileCount, MAX_TILE_COUNT);
            return false;
        }
        outIndex.resize(tileCount);
        for (uint32_t i = 0; i < tileCount; ++i)
        {
            outIndex[i].coordX = readLE<int32_t>(file);
            outIndex[i].coordZ = readLE<int32_t>(file);
            outIndex[i].heightDataOffset = readLE<uint64_t>(file);
            outIndex[i].heightDataSize = readLE<uint32_t>(file);
            outIndex[i].weightDataOffset = readLE<uint64_t>(file);
            outIndex[i].meshletDataOffset = readLE<uint64_t>(file);
            outIndex[i].holeMaskDataOffset = readLE<uint64_t>(file);
            outIndex[i].caveSdfDataOffset = readLE<uint64_t>(file);
            outIndex[i].payloadSize = readLE<uint32_t>(file);
        }
        return file.good();
    }

    bool TerrainSerializer::parseTileMeshletData(std::istream& file, TileLoadResult& result)
    {
        struct LODHeader
        {
            uint32_t meshletCount = 0;
            uint32_t meshletVertexCount = 0;
            uint32_t meshletPrimitiveCount = 0;
        };
        std::array<LODHeader, TERRAIN_LOD_COUNT> lodHeaders{};

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            lodHeaders[lod].meshletCount = readLE<uint32_t>(file);
            lodHeaders[lod].meshletVertexCount = readLE<uint32_t>(file);
            lodHeaders[lod].meshletPrimitiveCount = readLE<uint32_t>(file);
        }

        if (!file.good())
            return false;

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            const auto& header = lodHeaders[lod];
            if (header.meshletCount > MAX_MESHLETS_PER_LOD)
            {
                vfLogError("TerrainSerializer: Meshlet count {} exceeds maximum at LOD {}",
                           header.meshletCount, lod);
                return false;
            }

            const uint64_t maxMeshletVertices =
                static_cast<uint64_t>(header.meshletCount) * resource::MAX_MESHLET_VERTICES;
            const uint64_t maxMeshletPrimitives =
                static_cast<uint64_t>(header.meshletCount) * resource::MAX_MESHLET_PRIMITIVES;
            if (static_cast<uint64_t>(header.meshletVertexCount) > maxMeshletVertices ||
                static_cast<uint64_t>(header.meshletPrimitiveCount) > maxMeshletPrimitives)
            {
                vfLogError("TerrainSerializer: Invalid aggregate meshlet counts at LOD {}", lod);
                return false;
            }
        }

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            uint32_t vertexCount = readLE<uint32_t>(file);
            if (vertexCount > MAX_VERTICES_PER_LOD)
            {
                vfLogError("TerrainSerializer: Meshlet vertex count {} exceeds maximum at LOD {}", vertexCount, lod);
                return false;
            }
            result.lodData[lod].vertices.resize(vertexCount);

            for (uint32_t i = 0; i < vertexCount; ++i)
            {
                auto& vertex = result.lodData[lod].vertices[i];
                vertex.position.x = readLE<float>(file);
                vertex.position.y = readLE<float>(file);
                vertex.position.z = readLE<float>(file);
                vertex.normal.x = readLE<float>(file);
                vertex.normal.y = readLE<float>(file);
                vertex.normal.z = readLE<float>(file);
                vertex.texCoords.x = readLE<float>(file);
                vertex.texCoords.y = readLE<float>(file);
                vertex.boneIndices.x = readLE<int32_t>(file);
                vertex.boneIndices.y = readLE<int32_t>(file);
                vertex.boneIndices.z = readLE<int32_t>(file);
                vertex.boneIndices.w = readLE<int32_t>(file);
                vertex.boneWeights.x = readLE<float>(file);
                vertex.boneWeights.y = readLE<float>(file);
                vertex.boneWeights.z = readLE<float>(file);
                vertex.boneWeights.w = readLE<float>(file);
            }
        }

        if (!file.good())
            return false;

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            uint32_t indexCount = readLE<uint32_t>(file);
            if (indexCount > MAX_INDICES_PER_LOD)
            {
                vfLogError("TerrainSerializer: Index count {} exceeds maximum at LOD {}", indexCount, lod);
                return false;
            }
            readVectorLE(file, result.lodData[lod].indices, indexCount);
        }

        if (!file.good())
            return false;

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            result.lodData[lod].meshlets.resize(lodHeaders[lod].meshletCount);
            for (uint32_t i = 0; i < lodHeaders[lod].meshletCount; ++i)
            {
                auto& meshlet = result.lodData[lod].meshlets[i];

                meshlet.descriptor.vertexOffset = readLE<uint32_t>(file);
                meshlet.descriptor.primitiveOffset = readLE<uint32_t>(file);
                meshlet.descriptor.vertexCount = readLE<uint8_t>(file);
                meshlet.descriptor.primitiveCount = readLE<uint8_t>(file);
                meshlet.descriptor.padding = readLE<uint16_t>(file);

                meshlet.bounds.boundingSphere.x = readLE<float>(file);
                meshlet.bounds.boundingSphere.y = readLE<float>(file);
                meshlet.bounds.boundingSphere.z = readLE<float>(file);
                meshlet.bounds.boundingSphere.w = readLE<float>(file);
                meshlet.bounds.cone.x = readLE<float>(file);
                meshlet.bounds.cone.y = readLE<float>(file);
                meshlet.bounds.cone.z = readLE<float>(file);
                meshlet.bounds.cone.w = readLE<float>(file);
            }
        }

        if (!file.good())
            return false;

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            readVectorLE(file, result.lodData[lod].meshletVertices, lodHeaders[lod].meshletVertexCount);
        }

        if (!file.good())
            return false;

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            readVectorLE(file, result.lodData[lod].meshletPrimitives, lodHeaders[lod].meshletPrimitiveCount);
        }

        if (!file.good())
            return false;

        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            auto& lodData = result.lodData[lod];
            lodData.aabb.min.x = readLE<float>(file);
            lodData.aabb.min.y = readLE<float>(file);
            lodData.aabb.min.z = readLE<float>(file);
            lodData.aabb.max.x = readLE<float>(file);
            lodData.aabb.max.y = readLE<float>(file);
            lodData.aabb.max.z = readLE<float>(file);
            lodData.boundingSphere.x = readLE<float>(file);
            lodData.boundingSphere.y = readLE<float>(file);
            lodData.boundingSphere.z = readLE<float>(file);
            lodData.boundingSphere.w = readLE<float>(file);
            lodData.geometricError = readLE<float>(file);
        }

        if (!file.good())
            return false;

        result.hasLODCache = true;
        return true;
    }

    // Every non-zero sub-block offset must fall inside the record its entry describes, and records
    // must not overlap each other or the header/index. compact() copies records as opaque bytes and
    // shifts their offsets by one per-record delta, so these are the invariants that make that safe
    // even for a hostile or corrupt file — it does no decoding of its own to fall back on.
    bool TerrainSerializer::validateRecordExtents(const TerrainFileHeader& header,
                                                   const std::vector<TileIndexEntry>& index,
                                                   uint64_t indexTableOffset,
                                                   uint64_t fileSize)
    {
        const uint64_t payloadStart = indexTableOffset +
            static_cast<uint64_t>(header.tileCount) * TILE_INDEX_ENTRY_SIZE;

        std::vector<std::pair<uint64_t, uint64_t>> ranges;
        ranges.reserve(index.size());

        for (const auto& entry : index)
        {
            if (entry.heightDataOffset == 0 || entry.heightDataOffset >= fileSize)
            {
                vfLogError("TerrainSerializer: Invalid height data offset {} for tile ({}, {})",
                           entry.heightDataOffset, entry.coordX, entry.coordZ);
                return false;
            }
            if (entry.heightDataOffset < payloadStart)
            {
                vfLogError("TerrainSerializer: Tile ({}, {}) record overlaps the header or index table",
                           entry.coordX, entry.coordZ);
                return false;
            }
            if (entry.heightDataSize > fileSize - entry.heightDataOffset)
            {
                vfLogError("TerrainSerializer: Height data size {} exceeds file bounds for tile ({}, {})",
                           entry.heightDataSize, entry.coordX, entry.coordZ);
                return false;
            }
            if (entry.payloadSize == 0 || entry.payloadSize > MAX_TILE_PAYLOAD_BYTES ||
                entry.payloadSize < entry.heightDataSize ||
                entry.payloadSize > fileSize - entry.heightDataOffset)
            {
                vfLogError("TerrainSerializer: Invalid payload size {} for tile ({}, {})",
                           entry.payloadSize, entry.coordX, entry.coordZ);
                return false;
            }

            const uint64_t recordEnd = entry.heightDataOffset + entry.payloadSize;
            const std::array<uint64_t, 4> optionalOffsets = {
                entry.weightDataOffset,
                entry.meshletDataOffset,
                entry.holeMaskDataOffset,
                entry.caveSdfDataOffset
            };
            for (uint64_t offset : optionalOffsets)
            {
                if (offset != 0 && (offset < entry.heightDataOffset || offset >= recordEnd))
                {
                    vfLogError("TerrainSerializer: Optional data offset {} falls outside the record of tile ({}, {})",
                               offset, entry.coordX, entry.coordZ);
                    return false;
                }
            }

            ranges.emplace_back(entry.heightDataOffset, recordEnd);
        }

        std::sort(ranges.begin(), ranges.end());
        for (size_t i = 1; i < ranges.size(); ++i)
        {
            if (ranges[i].first < ranges[i - 1].second)
            {
                vfLogError("TerrainSerializer: Overlapping tile records at offsets {} and {}",
                           ranges[i - 1].first, ranges[i].first);
                return false;
            }
        }
        return true;
    }

    bool TerrainSerializer::readHeader(
        std::string_view path,
        TerrainFileHeader& outHeader,
        std::vector<TileIndexEntry>& outIndex,
        uint64_t* outIndexTableOffset)
    {
        std::shared_lock lock(terrainFileMutex());
        return readHeaderLocked(path, outHeader, outIndex, outIndexTableOffset);
    }

    bool TerrainSerializer::readHeaderLocked(
        std::string_view path,
        TerrainFileHeader& outHeader,
        std::vector<TileIndexEntry>& outIndex,
        uint64_t* outIndexTableOffset)
    {
        auto input = detail::openTerrainInputFile(std::string(path), 0);
        if (!input)
        {
            vfLogError("TerrainSerializer: File not found: {}", path);
            return false;
        }

        try
        {
            auto& file = input->stream;

            if (!parseHeader(file, outHeader))
                return false;

            const auto indexTableOffset = input->logicalPosition();
            if (!indexTableOffset)
                return false;
            if (outIndexTableOffset)
                *outIndexTableOffset = *indexTableOffset;

            const uint64_t indexBytes = static_cast<uint64_t>(outHeader.tileCount) *
                                        TILE_INDEX_ENTRY_SIZE;
            if (*indexTableOffset > input->location.size ||
                indexBytes > input->location.size - *indexTableOffset)
            {
                vfLogError("TerrainSerializer: Index table exceeds terrain file bounds");
                return false;
            }

            if (!parseIndexTable(file, outHeader.tileCount, outIndex))
            {
                vfLogError("TerrainSerializer: Failed to read index table");
                return false;
            }

            if (!input->logicalPosition())
            {
                vfLogError("TerrainSerializer: Index table crossed terrain file bounds");
                return false;
            }

            return validateRecordExtents(outHeader, outIndex, *indexTableOffset,
                                         input->location.size);
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to read header from {}: {}", path, e.what());
            return false;
        }
    }

    bool TerrainSerializer::readTileWeights(
        std::string_view path,
        const TileIndexEntry& entry,
        TileWeightMapData& outWeights)
    {
        if (entry.weightDataOffset == 0)
        {
            outWeights = TileWeightMapData{};
            return true;
        }

        std::shared_lock lock(terrainFileMutex());

        try
        {
            auto input = detail::openTerrainInputFile(std::string(path), entry.weightDataOffset);
            if (!input)
            {
                vfLogError("TerrainSerializer: Failed to open file: {}", path);
                return false;
            }

            auto& file = input->stream;

            for (uint8_t li = 0; li < WEIGHT_CHANNELS; ++li)
                outWeights.layerIndices[li] = readLE<uint8_t>(file);

            outWeights.resolution = readLE<uint32_t>(file);
            if (outWeights.resolution == 0 || outWeights.resolution > 257)
            {
                vfLogError("TerrainSerializer: Invalid weight map resolution {} for tile ({}, {})",
                           outWeights.resolution, entry.coordX, entry.coordZ);
                return false;
            }

            size_t texelCount = static_cast<size_t>(outWeights.resolution) * outWeights.resolution;
            outWeights.layerWeights.resize(WEIGHT_CHANNELS);
            for (uint8_t ch = 0; ch < WEIGHT_CHANNELS; ++ch)
            {
                // Compressed: read uint8 weights and dequantize to float
                std::vector<uint8_t> quantized(texelCount);
                file.read(reinterpret_cast<char*>(quantized.data()),
                          static_cast<std::streamsize>(texelCount));
                outWeights.layerWeights[ch] = compression::dequantizeWeights(quantized);
            }

            if (!file.good() || !input->logicalPosition())
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

    bool TerrainSerializer::readTileLODData(
        std::string_view path,
        const TileIndexEntry& entry,
        std::array<TileLODData, TERRAIN_LOD_COUNT>& outLODData)
    {
        if (entry.meshletDataOffset == 0)
        {
            return false;
        }

        std::shared_lock lock(terrainFileMutex());

        try
        {
            auto input = detail::openTerrainInputFile(std::string(path), entry.meshletDataOffset);
            if (!input)
            {
                vfLogError("TerrainSerializer: Failed to open file: {}", path);
                return false;
            }

            auto& file = input->stream;

            TileLoadResult tempResult;
            if (!parseTileMeshletData(file, tempResult))
            {
                vfLogError("TerrainSerializer: Failed to read LOD data for tile ({}, {})",
                           entry.coordX, entry.coordZ);
                return false;
            }

            if (!input->logicalPosition())
            {
                vfLogError("TerrainSerializer: LOD data crossed terrain file bounds");
                return false;
            }

            outLODData = std::move(tempResult.lodData);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to read LOD data for tile ({}, {}): {}",
                       entry.coordX, entry.coordZ, e.what());
            return false;
        }
    }

    bool TerrainSerializer::readTileHoleMask(
        std::string_view path,
        const TileIndexEntry& entry,
        std::vector<uint8_t>& outHoleMask)
    {
        if (entry.holeMaskDataOffset == 0)
        {
            outHoleMask.clear();
            return true;
        }

        std::shared_lock lock(terrainFileMutex());

        try
        {
            auto input = detail::openTerrainInputFile(std::string(path), entry.holeMaskDataOffset);
            if (!input)
            {
                vfLogError("TerrainSerializer: Failed to open file: {}", path);
                return false;
            }

            auto& file = input->stream;
            uint32_t totalVertices = readLE<uint32_t>(file);
            if (totalVertices > MAX_TILE_HOLE_QUADS)
            {
                vfLogError("TerrainSerializer: Hole count {} exceeds maximum {} for tile ({}, {})",
                           totalVertices, MAX_TILE_HOLE_QUADS, entry.coordX, entry.coordZ);
                return false;
            }

            const size_t packedSize = (static_cast<size_t>(totalVertices) + 7u) / 8u;
            std::vector<uint8_t> packed(packedSize);
            file.read(reinterpret_cast<char*>(packed.data()),
                      static_cast<std::streamsize>(packedSize));

            if (!file.good() || !input->logicalPosition())
            {
                vfLogError("TerrainSerializer: Read error for tile ({}, {}) hole mask",
                           entry.coordX, entry.coordZ);
                return false;
            }

            outHoleMask.resize(totalVertices, 0);
            for (uint32_t i = 0; i < totalVertices; ++i)
            {
                outHoleMask[i] = (packed[i / 8] >> (i % 8)) & 1;
            }

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to read hole mask for tile ({}, {}): {}",
                       entry.coordX, entry.coordZ, e.what());
            return false;
        }
    }
}
