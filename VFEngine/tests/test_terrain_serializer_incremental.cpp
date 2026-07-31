#include <doctest.h>

#include "test_terrain_serializer_fixture.hpp"

#include <algorithm>
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
            const terrain::TerrainStreamingConfig* streaming = nullptr)
        {
            terrain::TerrainIncrementalSaveParams params;
            params.path = file.string();
            params.grid = grid.get();
            params.dirtyCoords = &dirty;
            params.currentHeader = snapshot.header;
            params.indexTableOffset = snapshot.indexTableOffset;
            params.currentIndexMap = &indexMap;
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

        terrain::TerrainIncrementalSaveParams params;
        params.path = file.string();
        params.grid = grid.get();
        params.dirtyCoords = &dirty;
        params.currentHeader = snapshot.header;
        params.currentHeader.materialPath = requestedPath;
        params.indexTableOffset = snapshot.indexTableOffset;
        params.currentIndexMap = &indexMap;
        params.physicsConfig = snapshot.header.physicsConfig;
        params.streamingConfig = snapshot.header.streamingConfig;
        result.saveSucceeded = terrain::TerrainSerializer::saveIncremental(params);
        if (!result.saveSucceeded)
            return result;

        TerrainFileSnapshot reloaded;
        if (!readTerrainTestSnapshot(file.string(), reloaded) ||
            reloaded.header.materialPath != requestedPath)
        {
            return result;
        }

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

    TEST_CASE("material path length changes expose the current incremental header limitation")
    {
        const std::string originalPath = "materials/original.vfTerrainMat";
        const std::string sameLength(originalPath.size(), 's');

        const auto unchanged = terrainRoundTripsCleanlyWithMaterialPath(originalPath);
        CHECK(unchanged.saveSucceeded);
        CHECK(unchanged.roundTripsCleanly);

        const auto equalLength = terrainRoundTripsCleanlyWithMaterialPath(sameLength);
        CHECK(equalLength.saveSucceeded);
        CHECK(equalLength.roundTripsCleanly);

        for (const auto& changedLengthPath :
             {originalPath + "/longer", std::string("short.vfTerrainMat"), std::string()})
        {
            CAPTURE(changedLengthPath);
            const auto changed = terrainRoundTripsCleanlyWithMaterialPath(changedLengthPath);
            CHECK(changed.saveSucceeded);
            CHECK_FALSE(changed.roundTripsCleanly);
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
