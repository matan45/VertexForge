#pragma once

#include <terrain/TerrainGrid.hpp>
#include <terrain/TerrainSerializer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    namespace terrain_test_fs = std::filesystem;

    class ScopedTerrainTestFile
    {
    public:
        explicit ScopedTerrainTestFile(const std::string& stem = "terrain")
        {
            static std::atomic<uint64_t> sequence{0};
            const auto timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
            directory = terrain_test_fs::temp_directory_path() /
                ("vertexforge-terrain-" + std::to_string(timestamp) + "-" + std::to_string(id));
            terrain_test_fs::create_directories(directory);
            filePath = directory / (stem + ".vfTerrain");
            filePathString = filePath.string();
        }

        ScopedTerrainTestFile(const ScopedTerrainTestFile&) = delete;
        ScopedTerrainTestFile& operator=(const ScopedTerrainTestFile&) = delete;

        ~ScopedTerrainTestFile()
        {
            std::error_code ec;
            terrain_test_fs::remove(filePath, ec);
            terrain_test_fs::path tmpPath = filePath;
            tmpPath += ".tmp";
            terrain_test_fs::remove(tmpPath, ec);
            // The sidecar a save may have written alongside it, otherwise the directory
            // removal below fails and the temp dir leaks.
            terrain_test_fs::path metaPath = filePath;
            metaPath += ".vfmeta";
            terrain_test_fs::remove(metaPath, ec);
            terrain_test_fs::remove(directory, ec);
        }

        [[nodiscard]] const terrain_test_fs::path& path() const { return filePath; }
        [[nodiscard]] const std::string& string() const { return filePathString; }

    private:
        terrain_test_fs::path directory;
        terrain_test_fs::path filePath;
        std::string filePathString;
    };

    struct TerrainFileSnapshot
    {
        terrain::TerrainFileHeader header;
        std::vector<terrain::TileIndexEntry> index;
        uint64_t indexTableOffset = 0;
    };

    inline terrain::TerrainTileConfig makeTerrainTestConfig(terrain::TileResolution resolution)
    {
        terrain::TerrainTileConfig config;
        config.resolution = resolution;
        config.worldTileSize = 32.0f;
        config.skirtDepth = 2.0f;
        if (resolution == terrain::TileResolution::Low)
        {
            config.minHeight = -8.0f;
            config.maxHeight = 8.0f;
        }
        else
        {
            // Keep Medium/High cave grids small while still exercising a non-zero range.
            config.minHeight = -1.0f;
            config.maxHeight = 1.0f;
        }
        return config;
    }

    inline float terrainTestHeight(float worldX, float worldZ)
    {
        // Non-flat, deterministic, and bounded for the small coordinate sets used below.
        return -0.75f + worldX * worldX * 0.00015f + worldZ * 0.0025f;
    }

    inline void populateTerrainTestTile(terrain::TerrainTile& tile, bool retainOriginalSdf)
    {
        auto& weights = tile.weightMap;
        const size_t texelCount = weights.getTexelCount();
        weights.layerIndices = {7, 3, 11, 5, 2, 13, 17, 19};
        for (uint8_t channel = 0; channel < terrain::WEIGHT_CHANNELS; ++channel)
        {
            auto& channelWeights = weights.layerWeights[channel];
            channelWeights.resize(texelCount);
            for (size_t i = 0; i < texelCount; ++i)
            {
                channelWeights[i] =
                    static_cast<float>((i * 3 + static_cast<size_t>(channel) * 5) % 29) / 28.0f;
            }
        }

        tile.setHole(1, 2, true);
        tile.setHole(tile.config.getQuadCount() - 1, tile.config.getQuadCount() - 1, true);

        if (retainOriginalSdf)
            tile.initializeCaveSDFFromHeights();
        else
            tile.initializeCaveSDF();

        auto& cave = *tile.caveData;
        const uint32_t x = cave.config.resX / 2;
        const uint32_t y = cave.config.resY / 2;
        const uint32_t z = cave.config.resZ / 2;
        cave.setSDF(x, y, z, cave.getSDF(x, y, z) + 4.0f);
    }

    inline std::unique_ptr<terrain::TerrainGrid> makePopulatedTerrainTestGrid(
        const terrain::TerrainTileConfig& config,
        const std::vector<terrain::TileCoord>& coords,
        bool firstTileRetainsOriginalSdf = true)
    {
        auto grid = std::make_unique<terrain::TerrainGrid>(config);
        grid->setHeightSampler(
            [](float worldX, float worldZ) { return terrainTestHeight(worldX, worldZ); });

        for (size_t i = 0; i < coords.size(); ++i)
        {
            terrain::TerrainTile* tile = grid->addTile(coords[i]);
            populateTerrainTestTile(*tile, firstTileRetainsOriginalSdf && i == 0);
        }
        return grid;
    }

    inline terrain::TerrainSaveParams makeTerrainTestSaveParams(
        const std::string& path,
        const terrain::TerrainGrid& grid,
        const terrain::TerrainTileConfig& config,
        std::string materialPath = "materials/terrain/default.vfTerrainMat")
    {
        terrain::TerrainSaveParams params;
        params.path = path;
        params.grid = &grid;
        params.config = config;
        grid.computeBounds(params.gridMinX, params.gridMinZ, params.gridMaxX, params.gridMaxZ);
        params.materialPath = std::move(materialPath);
        params.physicsConfig.hasCollider = true;
        params.physicsConfig.collisionLayer = 6;
        params.physicsConfig.friction = 0.72f;
        params.physicsConfig.restitution = 0.08f;
        params.streamingConfig.enabled = true;
        params.streamingConfig.loadRadius = 384.0f;
        params.streamingConfig.unloadRadius = 448.0f;
        params.streamingConfig.maxLoadsPerFrame = 3;
        params.streamingConfig.maxUnloadsPerFrame = 2;
        return params;
    }

    inline bool readTerrainTestSnapshot(const std::string& path, TerrainFileSnapshot& out)
    {
        return terrain::TerrainSerializer::readHeader(
            path, out.header, out.index, &out.indexTableOffset);
    }

    inline std::unordered_map<terrain::TileCoord, terrain::TileIndexEntry, terrain::TileCoordHash>
    makeTerrainTestIndexMap(const std::vector<terrain::TileIndexEntry>& index)
    {
        std::unordered_map<terrain::TileCoord, terrain::TileIndexEntry, terrain::TileCoordHash> result;
        for (const auto& entry : index)
            result.emplace(terrain::TileCoord(entry.coordX, entry.coordZ), entry);
        return result;
    }

    inline const terrain::TileIndexEntry* findTerrainTestEntry(
        const std::vector<terrain::TileIndexEntry>& index,
        const terrain::TileCoord& coord)
    {
        const auto it = std::find_if(index.begin(), index.end(), [&coord](const auto& entry)
        {
            return entry.coordX == coord.x && entry.coordZ == coord.z;
        });
        return it == index.end() ? nullptr : &*it;
    }

    inline bool sameTerrainTestEntry(
        const terrain::TileIndexEntry& lhs,
        const terrain::TileIndexEntry& rhs)
    {
        return lhs.coordX == rhs.coordX &&
               lhs.coordZ == rhs.coordZ &&
               lhs.heightDataOffset == rhs.heightDataOffset &&
               lhs.heightDataSize == rhs.heightDataSize &&
               lhs.weightDataOffset == rhs.weightDataOffset &&
               lhs.meshletDataOffset == rhs.meshletDataOffset &&
               lhs.holeMaskDataOffset == rhs.holeMaskDataOffset &&
               lhs.caveSdfDataOffset == rhs.caveSdfDataOffset;
    }

    inline std::vector<uint8_t> readTerrainTestFileBytes(const terrain_test_fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>(
            std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    inline bool sameTerrainTestByteRange(
        const std::vector<uint8_t>& lhs,
        const std::vector<uint8_t>& rhs,
        uint64_t offset,
        uint64_t size)
    {
        if (offset > lhs.size() || offset > rhs.size() ||
            size > lhs.size() - static_cast<size_t>(offset) ||
            size > rhs.size() - static_cast<size_t>(offset))
        {
            return false;
        }
        return std::equal(
            lhs.begin() + static_cast<size_t>(offset),
            lhs.begin() + static_cast<size_t>(offset + size),
            rhs.begin() + static_cast<size_t>(offset));
    }

    inline bool equalTerrainTestVertex(
        const resource::Vertex& lhs,
        const resource::Vertex& rhs)
    {
        return lhs.position.x == rhs.position.x &&
               lhs.position.y == rhs.position.y &&
               lhs.position.z == rhs.position.z &&
               lhs.normal.x == rhs.normal.x &&
               lhs.normal.y == rhs.normal.y &&
               lhs.normal.z == rhs.normal.z &&
               lhs.texCoords.x == rhs.texCoords.x &&
               lhs.texCoords.y == rhs.texCoords.y &&
               lhs.boneIndices.x == rhs.boneIndices.x &&
               lhs.boneIndices.y == rhs.boneIndices.y &&
               lhs.boneIndices.z == rhs.boneIndices.z &&
               lhs.boneIndices.w == rhs.boneIndices.w &&
               lhs.boneWeights.x == rhs.boneWeights.x &&
               lhs.boneWeights.y == rhs.boneWeights.y &&
               lhs.boneWeights.z == rhs.boneWeights.z &&
               lhs.boneWeights.w == rhs.boneWeights.w;
    }

    inline bool equalTerrainTestLODData(
        const terrain::TileLODData& lhs,
        const terrain::TileLODData& rhs)
    {
        if (lhs.vertices.size() != rhs.vertices.size() ||
            lhs.indices != rhs.indices ||
            lhs.meshlets.size() != rhs.meshlets.size() ||
            lhs.meshletVertices != rhs.meshletVertices ||
            lhs.meshletPrimitives != rhs.meshletPrimitives)
        {
            return false;
        }

        for (size_t i = 0; i < lhs.vertices.size(); ++i)
        {
            if (!equalTerrainTestVertex(lhs.vertices[i], rhs.vertices[i]))
                return false;
        }
        for (size_t i = 0; i < lhs.meshlets.size(); ++i)
        {
            const auto& a = lhs.meshlets[i];
            const auto& b = rhs.meshlets[i];
            if (a.descriptor.vertexOffset != b.descriptor.vertexOffset ||
                a.descriptor.primitiveOffset != b.descriptor.primitiveOffset ||
                a.descriptor.vertexCount != b.descriptor.vertexCount ||
                a.descriptor.primitiveCount != b.descriptor.primitiveCount ||
                a.descriptor.padding != b.descriptor.padding ||
                a.bounds.boundingSphere.x != b.bounds.boundingSphere.x ||
                a.bounds.boundingSphere.y != b.bounds.boundingSphere.y ||
                a.bounds.boundingSphere.z != b.bounds.boundingSphere.z ||
                a.bounds.boundingSphere.w != b.bounds.boundingSphere.w ||
                a.bounds.cone.x != b.bounds.cone.x ||
                a.bounds.cone.y != b.bounds.cone.y ||
                a.bounds.cone.z != b.bounds.cone.z ||
                a.bounds.cone.w != b.bounds.cone.w)
            {
                return false;
            }
        }

        return lhs.aabb.min.x == rhs.aabb.min.x &&
               lhs.aabb.min.y == rhs.aabb.min.y &&
               lhs.aabb.min.z == rhs.aabb.min.z &&
               lhs.aabb.max.x == rhs.aabb.max.x &&
               lhs.aabb.max.y == rhs.aabb.max.y &&
               lhs.aabb.max.z == rhs.aabb.max.z &&
               lhs.boundingSphere.x == rhs.boundingSphere.x &&
               lhs.boundingSphere.y == rhs.boundingSphere.y &&
               lhs.boundingSphere.z == rhs.boundingSphere.z &&
               lhs.boundingSphere.w == rhs.boundingSphere.w &&
               lhs.geometricError == rhs.geometricError;
    }

    inline bool equalTerrainTestCaveData(
        const terrain::CaveSDFData& lhs,
        const terrain::CaveSDFData& rhs)
    {
        return lhs.config.resX == rhs.config.resX &&
               lhs.config.resY == rhs.config.resY &&
               lhs.config.resZ == rhs.config.resZ &&
               lhs.config.voxelSize == rhs.config.voxelSize &&
               lhs.config.yVoxelSize == rhs.config.yVoxelSize &&
               lhs.config.yExtentBelow == rhs.config.yExtentBelow &&
               lhs.localOrigin.x == rhs.localOrigin.x &&
               lhs.localOrigin.y == rhs.localOrigin.y &&
               lhs.localOrigin.z == rhs.localOrigin.z &&
               lhs.sdfGrid == rhs.sdfGrid &&
               lhs.originalSdfGrid == rhs.originalSdfGrid;
    }
}
