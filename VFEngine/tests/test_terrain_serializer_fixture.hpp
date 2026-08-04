#pragma once

#include <doctest.h>

#include <terrain/TerrainGrid.hpp>
#include <terrain/TerrainSaveFaultInjection.hpp>
#include <terrain/TerrainSaveJournal.hpp>
#include <terrain/TerrainSerializer.hpp>

#include <resource/EndianUtils.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
            // remove_all rather than remove: a crash-safety test deliberately leaves .tmp and
            // .vftrj artifacts behind, and a non-recursive remove would silently fail and leak the
            // whole temp directory. It also covers the .vfmeta sidecar a save writes alongside.
            std::error_code ec;
            terrain_test_fs::remove_all(directory, ec);
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
               lhs.caveSdfDataOffset == rhs.caveSdfDataOffset &&
               lhs.payloadSize == rhs.payloadSize;
    }

    inline terrain_test_fs::path terrainTestJournalPath(const ScopedTerrainTestFile& file)
    {
        return terrain::detail::terrainJournalPath(file.path());
    }

    inline terrain_test_fs::path terrainTestTempPath(const ScopedTerrainTestFile& file)
    {
        terrain_test_fs::path temp = file.path();
        temp += ".tmp";
        return temp;
    }

    inline uint64_t terrainTestFileSize(const ScopedTerrainTestFile& file)
    {
        std::error_code ec;
        const auto size = terrain_test_fs::file_size(file.path(), ec);
        return ec ? 0 : size;
    }

    inline terrain::TerrainFileOccupancy terrainTestOccupancy(const TerrainFileSnapshot& snapshot,
                                                              uint64_t fileSize)
    {
        return terrain::terrainFileOccupancy(snapshot.header, snapshot.index,
                                             snapshot.indexTableOffset, fileSize);
    }

    // Arms a save fault point for the duration of a scope. Saves run on a background thread in the
    // engine, and the injector is process-wide, so leaking one into the next test case would make
    // failures look like they belong to whichever test ran next.
    class ScopedTerrainFault
    {
    public:
        ScopedTerrainFault(terrain::TerrainSaveStage stage, uint64_t partialBytes)
        {
            terrain::setTerrainSaveFaultInjector(
                [stage, partialBytes](terrain::TerrainSaveStage current) -> terrain::TerrainSaveFault
                {
                    if (current != stage)
                        return {};
                    terrain::TerrainSaveFault fault;
                    fault.abort = true;
                    fault.partialBytes = partialBytes;
                    return fault;
                });
        }

        explicit ScopedTerrainFault(terrain::TerrainSaveStage stage)
            : ScopedTerrainFault(stage, (std::numeric_limits<uint64_t>::max)()) {}

        ScopedTerrainFault(const ScopedTerrainFault&) = delete;
        ScopedTerrainFault& operator=(const ScopedTerrainFault&) = delete;

        ~ScopedTerrainFault() { terrain::resetTerrainSaveFaultInjector(); }
    };

    // In-place field surgery on a real file: the way these suites synthesise a corrupt, torn or
    // hand-edited terrain without needing a second writer.
    template<typename T>
    inline bool terrainTestWriteValueAt(const terrain_test_fs::path& path, uint64_t offset, T value)
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open())
            return false;
        file.seekp(static_cast<std::streamoff>(offset));
        resource::endian::writeLE<T>(file, value);
        file.flush();
        return file.good();
    }

    template<typename T>
    inline bool terrainTestReadValueAt(const terrain_test_fs::path& path, uint64_t offset, T& value)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;
        file.seekg(static_cast<std::streamoff>(offset));
        value = resource::endian::readLE<T>(file);
        return file.good();
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

    using TerrainTestDirtySet =
        std::unordered_set<terrain::TileCoord, terrain::TileCoordHash>;

    // A saved three-tile terrain plus the cached header/index an incremental save needs. Shared by
    // the incremental and crash-safety suites, which exercise the same save through different
    // interruption points.
    struct IncrementalTerrainFixture
    {
        IncrementalTerrainFixture()
            : config(makeTerrainTestConfig(terrain::TileResolution::Low)),
              coords{{0, 0}, {1, 0}, {2, 0}},
              grid(makePopulatedTerrainTestGrid(config, coords, true)),
              file("incremental")
        {
            REQUIRE(terrain::TerrainSerializer::save(
                makeTerrainTestSaveParams(file.string(), *grid, config, materialPath)));
            REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
            indexMap = makeTerrainTestIndexMap(snapshot.index);
        }

        terrain::TerrainIncrementalSaveResult saveDirty(
            const TerrainTestDirtySet& dirty,
            const terrain::TerrainPhysicsConfig* physics = nullptr,
            const terrain::TerrainStreamingConfig* streaming = nullptr,
            const std::string* material = nullptr)
        {
            terrain::TerrainIncrementalSaveParams params;
            params.path = file.string();
            params.grid = grid.get();
            params.dirtyCoords = &dirty;
            params.currentHeader = snapshot.header;
            params.indexTableOffset = snapshot.indexTableOffset;
            params.currentIndexMap = &indexMap;
            // materialPath is the value to persist, not a "leave alone" sentinel — default it to
            // whatever is on disk so tests that are not about the material keep the header size.
            params.materialPath = material ? *material : snapshot.header.materialPath;
            params.physicsConfig = physics ? *physics : snapshot.header.physicsConfig;
            params.streamingConfig = streaming ? *streaming : snapshot.header.streamingConfig;
            return terrain::TerrainSerializer::saveIncremental(params);
        }

        bool saveDirtySucceeds(
            const TerrainTestDirtySet& dirty,
            const terrain::TerrainPhysicsConfig* physics = nullptr,
            const terrain::TerrainStreamingConfig* streaming = nullptr,
            const std::string* material = nullptr)
        {
            return saveDirty(dirty, physics, streaming, material) ==
                   terrain::TerrainIncrementalSaveResult::Success;
        }

        void refresh()
        {
            snapshot = TerrainFileSnapshot{};
            REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
            indexMap = makeTerrainTestIndexMap(snapshot.index);
        }

        terrain::TerrainTileConfig config;
        std::vector<terrain::TileCoord> coords;
        std::unique_ptr<terrain::TerrainGrid> grid;
        ScopedTerrainTestFile file;
        std::string materialPath = "mat/original.vfTerrainMat";
        TerrainFileSnapshot snapshot;
        std::unordered_map<terrain::TileCoord, terrain::TileIndexEntry, terrain::TileCoordHash>
            indexMap;
    };

    // Reads every persisted section of every indexed tile. Used to prove that an interrupted save
    // or a compaction left the file's tile data intact rather than merely parseable.
    inline bool allTerrainTestTilesRead(const ScopedTerrainTestFile& file,
                                        const std::vector<terrain::TileIndexEntry>& index)
    {
        for (const auto& entry : index)
        {
            std::vector<float> heights;
            terrain::TileWeightMapData weights;
            std::array<terrain::TileLODData, terrain::TERRAIN_LOD_COUNT> lodData;
            std::vector<uint8_t> holes;
            if (!terrain::TerrainSerializer::readTileHeights(file.string(), entry, heights) ||
                !terrain::TerrainSerializer::readTileWeights(file.string(), entry, weights) ||
                !terrain::TerrainSerializer::readTileLODData(file.string(), entry, lodData) ||
                !terrain::TerrainSerializer::readTileHoleMask(file.string(), entry, holes))
            {
                return false;
            }
        }
        return true;
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
