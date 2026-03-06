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

    static bool validateResolution(uint8_t res)
    {
        return res <= static_cast<uint8_t>(TileResolution::High);
    }

    static uint32_t resolutionToVertexCount(uint8_t res)
    {
        return TILE_VERTEX_COUNTS[res];
    }

    bool TerrainSerializer::parseHeader(std::ifstream& file, TerrainFileHeader& outHeader)
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
            outIndex[i].meshletDataOffset = readLE<uint64_t>(file);
            outIndex[i].holeMaskDataOffset = readLE<uint64_t>(file);
        }
        return file.good();
    }

    bool TerrainSerializer::parseTileMeshletData(std::ifstream& file, TileLoadResult& result)
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
            uint32_t vertexCount = readLE<uint32_t>(file);
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

                file.seekg(static_cast<std::streamoff>(entry.heightDataOffset));
                uint32_t heightCount = readLE<uint32_t>(file);
                if (heightCount != expectedHeightCount)
                {
                    vfLogError("TerrainSerializer: Height count mismatch for tile ({}, {}): got {}, expected {}",
                               entry.coordX, entry.coordZ, heightCount, expectedHeightCount);
                    return false;
                }
                readVectorLE(file, result.heightData, heightCount);

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

                if (hasFlag(outHeader.flags, TerrainFormatFlags::HAS_MESHLET_CACHE)
                    && entry.meshletDataOffset != 0)
                {
                    file.seekg(static_cast<std::streamoff>(entry.meshletDataOffset));
                    if (!parseTileMeshletData(file, result))
                    {
                        vfLogWarning("TerrainSerializer: Failed to read meshlet cache for tile ({}, {}), will regenerate",
                                     entry.coordX, entry.coordZ);
                        for (auto& lod : result.lodData)
                            lod.clear();
                        result.hasLODCache = false;
                    }
                }

                if (hasFlag(outHeader.flags, TerrainFormatFlags::HAS_HOLE_MASK)
                    && entry.holeMaskDataOffset != 0)
                {
                    file.seekg(static_cast<std::streamoff>(entry.holeMaskDataOffset));
                    uint32_t totalVertices = readLE<uint32_t>(file);
                    uint32_t packedSize = (totalVertices + 7) / 8;
                    std::vector<uint8_t> packed(packedSize);
                    file.read(reinterpret_cast<char*>(packed.data()),
                              static_cast<std::streamsize>(packedSize));

                    result.holeMask.resize(totalVertices, 0);
                    for (uint32_t j = 0; j < totalVertices; ++j)
                    {
                        result.holeMask[j] = (packed[j / 8] >> (j % 8)) & 1;
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

    bool TerrainSerializer::readTileLODData(
        std::string_view path,
        const TileIndexEntry& entry,
        std::array<TileLODData, TERRAIN_LOD_COUNT>& outLODData)
    {
        if (entry.meshletDataOffset == 0)
        {
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

            file.seekg(static_cast<std::streamoff>(entry.meshletDataOffset));

            TileLoadResult tempResult;
            if (!parseTileMeshletData(file, tempResult))
            {
                vfLogError("TerrainSerializer: Failed to read LOD data for tile ({}, {})",
                           entry.coordX, entry.coordZ);
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

        try
        {
            std::ifstream file(fs::path(path), std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to open file: {}", path);
                return false;
            }

            file.seekg(static_cast<std::streamoff>(entry.holeMaskDataOffset));
            uint32_t totalVertices = readLE<uint32_t>(file);
            uint32_t packedSize = (totalVertices + 7) / 8;
            std::vector<uint8_t> packed(packedSize);
            file.read(reinterpret_cast<char*>(packed.data()),
                      static_cast<std::streamsize>(packedSize));

            outHoleMask.resize(totalVertices, 0);
            for (uint32_t i = 0; i < totalVertices; ++i)
            {
                outHoleMask[i] = (packed[i / 8] >> (i % 8)) & 1;
            }

            if (!file.good())
            {
                vfLogError("TerrainSerializer: Read error for tile ({}, {}) hole mask",
                           entry.coordX, entry.coordZ);
                return false;
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
