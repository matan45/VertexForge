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
        result.saveSucceeded = terrain::TerrainSerializer::saveIncremental(params) ==
                               terrain::TerrainIncrementalSaveResult::Success;
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
        REQUIRE(fixture.saveDirtySucceeds(dirty));

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
            REQUIRE(fixture.saveDirtySucceeds(dirty));
            fixture.refresh();

            const uint64_t newSize = terrain_test_fs::file_size(fixture.file.path());
            CHECK(newSize > previousSize);
            // Growth is only unbounded below the compaction threshold. Assert the coupling rather
            // than leave it accidental: three Low-resolution records are far under the 1 MiB floor,
            // so nothing has compacted here and the monotonic-growth expectation above still holds.
            CHECK_FALSE(terrain::shouldCompactTerrainFile(
                terrainTestOccupancy(fixture.snapshot, newSize)));
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

    TEST_CASE("an incremental save never clears a feature flag the resident tiles cannot justify")
    {
        // computeFlags() only sees the tiles in the grid. On the incremental path that is a subset
        // of the file — with streaming on, most tiles are not resident — so taking its answer
        // verbatim would clear flags the streamed-out tiles still depend on. Worse, writeTileData()
        // gates each optional section on those flags, so the tiles being rewritten would lose that
        // data outright. The flags must therefore only ever be unioned.
        IncrementalTerrainFixture fixture;
        REQUIRE(hasFlag(fixture.snapshot.header.flags, terrain::TerrainFormatFlags::HAS_CAVE_DATA));

        const auto* untouchedEntry = findTerrainTestEntry(fixture.snapshot.index, fixture.coords[0]);
        REQUIRE(untouchedEntry != nullptr);
        REQUIRE(untouchedEntry->caveSdfDataOffset != 0);

        // Stand in for "the tiles that justified this flag are no longer in memory".
        for (const auto& coord : fixture.coords)
        {
            terrain::TerrainTile* tile = fixture.grid->getTile(coord);
            REQUIRE(tile != nullptr);
            tile->caveData.reset();
        }

        terrain::TerrainTile* dirtyTile = fixture.grid->getTile(fixture.coords[1]);
        REQUIRE(dirtyTile != nullptr);
        dirtyTile->heightData[0] += 0.25f;
        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> dirty = {
            fixture.coords[1]
        };
        REQUIRE(fixture.saveDirtySucceeds(dirty));
        fixture.refresh();

        CHECK(hasFlag(fixture.snapshot.header.flags, terrain::TerrainFormatFlags::HAS_CAVE_DATA));

        // And the untouched tile's cave data is still both indexed and readable.
        const auto* reloaded = findTerrainTestEntry(fixture.snapshot.index, fixture.coords[0]);
        REQUIRE(reloaded != nullptr);
        CHECK(reloaded->caveSdfDataOffset == untouchedEntry->caveSdfDataOffset);
        terrain::CaveSDFData caveData;
        CHECK(terrain::TerrainSerializer::readTileCaveData(fixture.file.string(), *reloaded, caveData));
    }

    TEST_CASE("an incremental save preserves header flag bits it does not know about")
    {
        // Bit 6 is reserved for HAS_EDIT_LAYER_SIDECAR. Assigning computeFlags()' answer wholesale
        // would silently drop it, and the sidecar it marks would become an orphan.
        IncrementalTerrainFixture fixture;
        constexpr uint32_t reservedBit = 1u << 6;
        constexpr uint64_t flagsFieldOffset = 4 + 3 * sizeof(uint32_t);

        uint32_t onDiskFlags = 0;
        REQUIRE(terrainTestReadValueAt<uint32_t>(fixture.file.path(), flagsFieldOffset, onDiskFlags));
        REQUIRE((onDiskFlags & reservedBit) == 0);
        REQUIRE(terrainTestWriteValueAt<uint32_t>(
            fixture.file.path(), flagsFieldOffset, onDiskFlags | reservedBit));

        fixture.refresh();
        REQUIRE((static_cast<uint32_t>(fixture.snapshot.header.flags) & reservedBit) != 0);

        terrain::TerrainTile* dirtyTile = fixture.grid->getTile(fixture.coords[1]);
        REQUIRE(dirtyTile != nullptr);
        dirtyTile->heightData[0] += 0.125f;
        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> dirty = {
            fixture.coords[1]
        };
        REQUIRE(fixture.saveDirtySucceeds(dirty));
        fixture.refresh();

        CHECK((static_cast<uint32_t>(fixture.snapshot.header.flags) & reservedBit) != 0);
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
            REQUIRE(fixture.saveDirtySucceeds(dirty, nullptr, nullptr, &path));

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
        CHECK(terrain::TerrainSerializer::saveIncremental(params) ==
              terrain::TerrainIncrementalSaveResult::Success);
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);

        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> empty;
        params.dirtyCoords = &empty;
        CHECK(terrain::TerrainSerializer::saveIncremental(params) ==
              terrain::TerrainIncrementalSaveResult::Success);
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

        using Result = terrain::TerrainIncrementalSaveResult;

        // Malformed arguments are a caller bug, not a layout problem — they must NOT report
        // NeedsFullSave, because the service answers that by full-saving from whatever tiles happen
        // to be resident.
        CHECK(terrain::TerrainSerializer::saveIncremental(params) == Result::Failed);
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);

        params.grid = fixture.grid.get();
        params.currentIndexMap = nullptr;
        CHECK(terrain::TerrainSerializer::saveIncremental(params) == Result::Failed);
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);

        ScopedTerrainTestFile missing("missing");
        params.path = missing.string();
        params.currentIndexMap = &fixture.indexMap;
        CHECK(terrain::TerrainSerializer::saveIncremental(params) == Result::Failed);
        CHECK_FALSE(terrain_test_fs::exists(missing.path()));

        // A flag toggle resizes the header and moves the index table. That one IS a layout
        // problem, and the full-save fallback is the correct answer to it.
        params.path = fixture.file.string();
        auto toggledPhysics = fixture.snapshot.header.physicsConfig;
        toggledPhysics.hasCollider = false;
        params.physicsConfig = toggledPhysics;
        CHECK(terrain::TerrainSerializer::saveIncremental(params) == Result::NeedsFullSave);
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);

        params.physicsConfig = fixture.snapshot.header.physicsConfig;
        auto toggledStreaming = fixture.snapshot.header.streamingConfig;
        toggledStreaming.enabled = false;
        params.streamingConfig = toggledStreaming;
        CHECK(terrain::TerrainSerializer::saveIncremental(params) == Result::NeedsFullSave);
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);
        CHECK_FALSE(terrain_test_fs::exists(terrainTestJournalPath(fixture.file)));
    }

    TEST_CASE("dirty coordinates absent from the cached index are refused, not appended unindexed")
    {
        // Writing a record no index entry can point at produced pure garbage AND lost the tile's
        // edits anyway, because refreshIndex() clears the dirty set either way. The save is now
        // refused so the caller full-saves, which does have a slot for the tile.
        IncrementalTerrainFixture fixture;
        const auto missingCoord = fixture.coords[1];
        fixture.indexMap.erase(missingCoord);
        const auto before = readTerrainTestFileBytes(fixture.file.path());
        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> dirty = {missingCoord};

        CHECK(fixture.saveDirty(dirty) == terrain::TerrainIncrementalSaveResult::NeedsFullSave);
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);
        CHECK_FALSE(terrain_test_fs::exists(terrainTestJournalPath(fixture.file)));

        TerrainFileSnapshot reloaded;
        REQUIRE(readTerrainTestSnapshot(fixture.file.string(), reloaded));
        CHECK(findTerrainTestEntry(reloaded.index, missingCoord) != nullptr);
    }
}
