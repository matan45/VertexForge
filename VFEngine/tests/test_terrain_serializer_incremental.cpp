#include <doctest.h>

#include "test_terrain_serializer_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
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

        bool saveDirty(
            const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash>& dirty,
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

    struct MaterialPathIncrementalResult
    {
        bool saveSucceeded = false;
        bool roundTripsCleanly = false;
        bool fileUnchanged = false;
        std::string persistedPath;
    };

    MaterialPathIncrementalResult terrainRoundTripsCleanlyWithMaterialPath(
        const std::string& requestedPath)
    {
        MaterialPathIncrementalResult result;
        const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
        const std::vector<terrain::TileCoord> coords = {{0, 0}};
        auto grid = makePopulatedTerrainTestGrid(config, coords, true);
        ScopedTerrainTestFile file("material-path");
        const std::string originalPath = "materials/original.vfTerrainMat";
        if (!terrain::TerrainSerializer::save(
                makeTerrainTestSaveParams(file.string(), *grid, config, originalPath)))
        {
            return result;
        }

        TerrainFileSnapshot snapshot;
        if (!readTerrainTestSnapshot(file.string(), snapshot))
            return result;
        auto indexMap = makeTerrainTestIndexMap(snapshot.index);
        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> dirty = {coords.front()};
        const auto beforeBytes = readTerrainTestFileBytes(file.path());

        terrain::TerrainIncrementalSaveParams params;
        params.path = file.string();
        params.grid = grid.get();
        params.dirtyCoords = &dirty;
        // currentHeader stays the on-disk truth; the requested path travels in its own field.
        params.currentHeader = snapshot.header;
        params.materialPath = requestedPath;
        params.indexTableOffset = snapshot.indexTableOffset;
        params.currentIndexMap = &indexMap;
        params.physicsConfig = snapshot.header.physicsConfig;
        params.streamingConfig = snapshot.header.streamingConfig;
        result.saveSucceeded = terrain::TerrainSerializer::saveIncremental(params);
        result.fileUnchanged = readTerrainTestFileBytes(file.path()) == beforeBytes;
        if (!result.saveSucceeded)
            return result;

        TerrainFileSnapshot reloaded;
        if (!readTerrainTestSnapshot(file.string(), reloaded))
            return result;
        result.persistedPath = reloaded.header.materialPath;

        const auto* entry = findTerrainTestEntry(reloaded.index, coords.front());
        std::vector<float> heights;
        result.roundTripsCleanly =
            entry != nullptr &&
            terrain::TerrainSerializer::readTileHeights(file.string(), *entry, heights) &&
            heights.size() == grid->getTile(coords.front())->heightData.size();
        return result;
    }
}

TEST_SUITE("TerrainSerializerIncremental")
{
    TEST_CASE("dirty payloads append while unchanged payloads and old bytes remain intact")
    {
        IncrementalTerrainFixture fixture;
        const auto beforeBytes = readTerrainTestFileBytes(fixture.file.path());
        const uint64_t oldFileSize = beforeBytes.size();
        const auto beforeIndex = fixture.snapshot.index;

        terrain::TerrainTile* dirtyTile = fixture.grid->getTile(fixture.coords[1]);
        REQUIRE(dirtyTile != nullptr);
        dirtyTile->heightData[0] += 0.375f;
        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> dirty = {
            fixture.coords[1]
        };
        REQUIRE(fixture.saveDirty(dirty));

        fixture.refresh();
        const auto afterBytes = readTerrainTestFileBytes(fixture.file.path());
        const auto* oldDirtyEntry = findTerrainTestEntry(beforeIndex, fixture.coords[1]);
        const auto* newDirtyEntry = findTerrainTestEntry(fixture.snapshot.index, fixture.coords[1]);
        REQUIRE(oldDirtyEntry != nullptr);
        REQUIRE(newDirtyEntry != nullptr);
        CHECK(newDirtyEntry->heightDataOffset >= oldFileSize);
        CHECK(sameTerrainTestByteRange(
            beforeBytes, afterBytes, oldDirtyEntry->heightDataOffset, oldDirtyEntry->heightDataSize));

        for (const auto unchangedCoord : {fixture.coords[0], fixture.coords[2]})
        {
            CAPTURE(unchangedCoord.x);
            CAPTURE(unchangedCoord.z);
            const auto* before = findTerrainTestEntry(beforeIndex, unchangedCoord);
            const auto* after = findTerrainTestEntry(fixture.snapshot.index, unchangedCoord);
            REQUIRE(before != nullptr);
            REQUIRE(after != nullptr);
            CHECK(sameTerrainTestEntry(*before, *after));

            uint64_t payloadEnd = oldFileSize;
            for (const auto& candidate : beforeIndex)
            {
                if (candidate.heightDataOffset > before->heightDataOffset)
                    payloadEnd = std::min(payloadEnd, candidate.heightDataOffset);
            }
            REQUIRE(payloadEnd >= before->heightDataOffset);
            CHECK(sameTerrainTestByteRange(
                beforeBytes,
                afterBytes,
                before->heightDataOffset,
                payloadEnd - before->heightDataOffset));
        }
    }

    TEST_CASE("repeated dirty saves grow monotonically and preserve index metadata and order")
    {
        IncrementalTerrainFixture fixture;
        const uint64_t originalIndexOffset = fixture.snapshot.indexTableOffset;
        const uint32_t originalTileCount = fixture.snapshot.header.tileCount;
        const auto originalFlags = fixture.snapshot.header.flags;
        uint64_t previousSize = terrain_test_fs::file_size(fixture.file.path());

        for (uint32_t pass = 0; pass < 3; ++pass)
        {
            terrain::TerrainTile* tile = fixture.grid->getTile(fixture.coords[0]);
            REQUIRE(tile != nullptr);
            tile->heightData[pass] += 0.125f * static_cast<float>(pass + 1);
            const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> dirty = {
                fixture.coords[0]
            };
            REQUIRE(fixture.saveDirty(dirty));
            fixture.refresh();

            const uint64_t newSize = terrain_test_fs::file_size(fixture.file.path());
            CHECK(newSize > previousSize);
            CHECK(fixture.snapshot.indexTableOffset == originalIndexOffset);
            CHECK(fixture.snapshot.header.tileCount == originalTileCount);
            CHECK(static_cast<uint32_t>(fixture.snapshot.header.flags) ==
                  static_cast<uint32_t>(originalFlags));
            REQUIRE(fixture.snapshot.index.size() == fixture.coords.size());
            CHECK(std::is_sorted(
                fixture.snapshot.index.begin(),
                fixture.snapshot.index.end(),
                [](const auto& lhs, const auto& rhs)
                {
                    return lhs.coordX < rhs.coordX ||
                           (lhs.coordX == rhs.coordX && lhs.coordZ < rhs.coordZ);
                }));
            previousSize = newSize;
        }
    }

    TEST_CASE("material path persists in place at equal length and is refused at any other length")
    {
        const std::string originalPath = "materials/original.vfTerrainMat";
        const std::string sameLength(originalPath.size(), 's');

        const auto unchanged = terrainRoundTripsCleanlyWithMaterialPath(originalPath);
        CHECK(unchanged.saveSucceeded);
        CHECK(unchanged.roundTripsCleanly);
        CHECK(unchanged.persistedPath == originalPath);

        // Equal length keeps the index table where it is, so the swap is safe in place.
        const auto equalLength = terrainRoundTripsCleanlyWithMaterialPath(sameLength);
        CHECK(equalLength.saveSucceeded);
        CHECK(equalLength.roundTripsCleanly);
        CHECK(equalLength.persistedPath == sameLength);

        // Any other length moves the index table. The save must be refused so the caller
        // falls back to a full save, and the file must be left byte-for-byte untouched.
        for (const auto& changedLengthPath :
             {originalPath + "/longer", std::string("short.vfTerrainMat"), std::string()})
        {
            CAPTURE(changedLengthPath);
            const auto changed = terrainRoundTripsCleanlyWithMaterialPath(changedLengthPath);
            CHECK_FALSE(changed.saveSucceeded);
            CHECK(changed.fileUnchanged);
        }
    }

    TEST_CASE("serializedHeaderSize matches the index table offset the writer produces")
    {
        // saveIncremental() rewrites the header in place and then seeks to the cached index
        // table offset, so serializedHeaderSize() must agree with writeHeader() exactly.
        const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
        const std::vector<terrain::TileCoord> coords = {{0, 0}};
        auto grid = makePopulatedTerrainTestGrid(config, coords, true);

        const std::array<std::string, 3> materialPaths = {
            std::string(),
            std::string("m.vfTerrainMat"),
            std::string(200, 'p')
        };

        for (const bool withPhysics : {false, true})
        {
            for (const bool withStreaming : {false, true})
            {
                for (const auto& materialPath : materialPaths)
                {
                    CAPTURE(withPhysics);
                    CAPTURE(withStreaming);
                    CAPTURE(materialPath.size());

                    ScopedTerrainTestFile file("header-size");
                    auto params = makeTerrainTestSaveParams(file.string(), *grid, config, materialPath);
                    params.physicsConfig.hasCollider = withPhysics;
                    params.streamingConfig.enabled = withStreaming;
                    REQUIRE(terrain::TerrainSerializer::save(params));

                    TerrainFileSnapshot snapshot;
                    REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
                    CHECK(terrain::serializedHeaderSize(snapshot.header) == snapshot.indexTableOffset);
                }
            }
        }
    }

    TEST_CASE("repeated equal-length material path changes keep the index table consistent")
    {
        IncrementalTerrainFixture fixture;
        const std::string firstPath(fixture.materialPath.size(), 'a');
        const std::string secondPath(fixture.materialPath.size(), 'b');

        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> dirty = {
            fixture.coords[1]
        };

        for (const auto& path : {firstPath, secondPath})
        {
            CAPTURE(path);
            const auto beforeIndex = fixture.snapshot.index;
            const auto beforeBytes = readTerrainTestFileBytes(fixture.file.path());

            terrain::TerrainTile* dirtyTile = fixture.grid->getTile(fixture.coords[1]);
            REQUIRE(dirtyTile != nullptr);
            dirtyTile->heightData[0] += 0.125f;
            REQUIRE(fixture.saveDirty(dirty, nullptr, nullptr, &path));

            fixture.refresh();
            CHECK(fixture.snapshot.header.materialPath == path);
            CHECK(terrain::serializedHeaderSize(fixture.snapshot.header) ==
                  fixture.snapshot.indexTableOffset);

            // The untouched tiles must keep both their index entries and their payload bytes.
            const auto afterBytes = readTerrainTestFileBytes(fixture.file.path());
            for (const auto unchangedCoord : {fixture.coords[0], fixture.coords[2]})
            {
                CAPTURE(unchangedCoord.x);
                CAPTURE(unchangedCoord.z);
                const auto* before = findTerrainTestEntry(beforeIndex, unchangedCoord);
                const auto* after = findTerrainTestEntry(fixture.snapshot.index, unchangedCoord);
                REQUIRE(before != nullptr);
                REQUIRE(after != nullptr);
                CHECK(sameTerrainTestEntry(*before, *after));
                CHECK(sameTerrainTestByteRange(
                    beforeBytes, afterBytes, before->heightDataOffset, before->heightDataSize));
            }
        }
    }

    TEST_CASE("empty dirty sets return success without touching the file")
    {
        IncrementalTerrainFixture fixture;
        const auto before = readTerrainTestFileBytes(fixture.file.path());

        terrain::TerrainIncrementalSaveParams params;
        params.path = fixture.file.string();
        params.grid = nullptr;
        params.currentIndexMap = nullptr;
        params.dirtyCoords = nullptr;
        CHECK(terrain::TerrainSerializer::saveIncremental(params));
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);

        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> empty;
        params.dirtyCoords = &empty;
        CHECK(terrain::TerrainSerializer::saveIncremental(params));
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);
    }

    TEST_CASE("invalid incremental inputs and header flag changes fail without file mutation")
    {
        IncrementalTerrainFixture fixture;
        const auto before = readTerrainTestFileBytes(fixture.file.path());
        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> dirty = {
            fixture.coords[0]
        };

        terrain::TerrainIncrementalSaveParams params;
        params.path = fixture.file.string();
        params.grid = nullptr;
        params.dirtyCoords = &dirty;
        params.currentHeader = fixture.snapshot.header;
        params.indexTableOffset = fixture.snapshot.indexTableOffset;
        params.currentIndexMap = &fixture.indexMap;
        // Keep the persisted material path identical so the toggle cases below isolate the
        // flag change rather than failing on a header-size difference.
        params.materialPath = fixture.snapshot.header.materialPath;
        params.physicsConfig = fixture.snapshot.header.physicsConfig;
        params.streamingConfig = fixture.snapshot.header.streamingConfig;
        CHECK_FALSE(terrain::TerrainSerializer::saveIncremental(params));
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);

        params.grid = fixture.grid.get();
        params.currentIndexMap = nullptr;
        CHECK_FALSE(terrain::TerrainSerializer::saveIncremental(params));
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);

        ScopedTerrainTestFile missing("missing");
        params.path = missing.string();
        params.currentIndexMap = &fixture.indexMap;
        CHECK_FALSE(terrain::TerrainSerializer::saveIncremental(params));
        CHECK_FALSE(terrain_test_fs::exists(missing.path()));

        params.path = fixture.file.string();
        auto toggledPhysics = fixture.snapshot.header.physicsConfig;
        toggledPhysics.hasCollider = false;
        params.physicsConfig = toggledPhysics;
        CHECK_FALSE(terrain::TerrainSerializer::saveIncremental(params));
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);

        params.physicsConfig = fixture.snapshot.header.physicsConfig;
        auto toggledStreaming = fixture.snapshot.header.streamingConfig;
        toggledStreaming.enabled = false;
        params.streamingConfig = toggledStreaming;
        CHECK_FALSE(terrain::TerrainSerializer::saveIncremental(params));
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);
    }

    TEST_CASE("dirty coordinates absent from the cached index append data but remain unindexed")
    {
        IncrementalTerrainFixture fixture;
        const auto missingCoord = fixture.coords[1];
        fixture.indexMap.erase(missingCoord);
        const uint64_t oldSize = terrain_test_fs::file_size(fixture.file.path());
        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> dirty = {missingCoord};

        REQUIRE(fixture.saveDirty(dirty));
        CHECK(terrain_test_fs::file_size(fixture.file.path()) > oldSize);

        TerrainFileSnapshot reloaded;
        REQUIRE(readTerrainTestSnapshot(fixture.file.string(), reloaded));
        CHECK(findTerrainTestEntry(reloaded.index, missingCoord) == nullptr);
    }
}
