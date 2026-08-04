#include <doctest.h>

#include "test_terrain_serializer_fixture.hpp"

#include <terrain/TerrainFileAccess.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

// VK-1644. Every case here interrupts a save at a specific point and then asserts what survives.
// The invariant under test is the same one throughout: at no instant is the .vfTerrain neither the
// complete previous generation nor the complete new one.
namespace
{
    using Result = terrain::TerrainIncrementalSaveResult;
    using Recovery = terrain::TerrainRecoveryResult;
    using Stage = terrain::TerrainSaveStage;

    // Everything a tile persists, decoded. Compaction moves records as opaque bytes, so this is
    // what proves it moved them faithfully rather than merely producing a parseable file.
    struct DecodedTile
    {
        std::vector<float> heights;
        terrain::TileWeightMapData weights;
        std::array<terrain::TileLODData, terrain::TERRAIN_LOD_COUNT> lodData;
        std::vector<uint8_t> holeMask;
    };

    bool decodeTiles(const ScopedTerrainTestFile& file,
                     const std::vector<terrain::TileIndexEntry>& index,
                     std::vector<DecodedTile>& out)
    {
        out.clear();
        for (const auto& entry : index)
        {
            DecodedTile tile;
            if (!terrain::TerrainSerializer::readTileHeights(file.string(), entry, tile.heights) ||
                !terrain::TerrainSerializer::readTileWeights(file.string(), entry, tile.weights) ||
                !terrain::TerrainSerializer::readTileLODData(file.string(), entry, tile.lodData) ||
                !terrain::TerrainSerializer::readTileHoleMask(file.string(), entry, tile.holeMask))
            {
                return false;
            }
            out.push_back(std::move(tile));
        }
        return true;
    }

    bool sameDecodedTiles(const std::vector<DecodedTile>& lhs, const std::vector<DecodedTile>& rhs)
    {
        if (lhs.size() != rhs.size())
            return false;
        for (size_t i = 0; i < lhs.size(); ++i)
        {
            if (lhs[i].heights != rhs[i].heights || lhs[i].holeMask != rhs[i].holeMask)
                return false;
            if (lhs[i].weights.layerIndices != rhs[i].weights.layerIndices ||
                lhs[i].weights.resolution != rhs[i].weights.resolution ||
                lhs[i].weights.layerWeights != rhs[i].weights.layerWeights)
                return false;
            for (size_t lod = 0; lod < terrain::TERRAIN_LOD_COUNT; ++lod)
            {
                if (!equalTerrainTestLODData(lhs[i].lodData[lod], rhs[i].lodData[lod]))
                    return false;
            }
        }
        return true;
    }

    std::vector<uint8_t> indexRegionBytes(const ScopedTerrainTestFile& file,
                                          const TerrainFileSnapshot& snapshot)
    {
        const auto bytes = readTerrainTestFileBytes(file.path());
        const size_t begin = static_cast<size_t>(snapshot.indexTableOffset);
        const size_t size =
            static_cast<size_t>(snapshot.header.tileCount) * terrain::TILE_INDEX_ENTRY_SIZE;
        if (begin + size > bytes.size())
            return {};
        return std::vector<uint8_t>(bytes.begin() + static_cast<ptrdiff_t>(begin),
                                    bytes.begin() + static_cast<ptrdiff_t>(begin + size));
    }

    // Reports archive mode while still resolving files normally, so the "writes are refused inside
    // a .vfpak" paths can be exercised against a real file on disk.
    class ScopedArchiveMode
    {
    public:
        ScopedArchiveMode()
        {
            terrain::TerrainFileAccess access;
            access.locate = [](const std::string& path) -> std::optional<terrain::TerrainFileLocation>
            {
                std::error_code ec;
                const uint64_t size = terrain_test_fs::file_size(path, ec);
                if (ec) return std::nullopt;
                return terrain::TerrainFileLocation{path, 0, size};
            };
            access.readBytes = [](const std::string&) { return std::vector<uint8_t>{}; };
            access.exists = [](const std::string& path)
            {
                std::error_code ec;
                return terrain_test_fs::exists(path, ec) && !ec;
            };
            access.isArchiveMode = [] { return true; };
            terrain::setTerrainFileAccess(std::move(access));
        }

        ScopedArchiveMode(const ScopedArchiveMode&) = delete;
        ScopedArchiveMode& operator=(const ScopedArchiveMode&) = delete;

        ~ScopedArchiveMode() { terrain::resetTerrainFileAccess(); }
    };

    terrain::TileCoord dirtyOneTile(IncrementalTerrainFixture& fixture, size_t which, float delta)
    {
        terrain::TerrainTile* tile = fixture.grid->getTile(fixture.coords[which]);
        REQUIRE(tile != nullptr);
        tile->heightData[0] += delta;
        return fixture.coords[which];
    }
}

TEST_SUITE("TerrainSerializerCrashSafety")
{
    TEST_CASE("an interrupted payload append leaves the previous generation complete")
    {
        IncrementalTerrainFixture fixture;
        const auto beforeBytes = readTerrainTestFileBytes(fixture.file.path());
        const auto beforeIndex = fixture.snapshot.index;
        const uint64_t beforeSize = beforeBytes.size();

        const TerrainTestDirtySet dirty = {dirtyOneTile(fixture, 1, 0.375f)};
        {
            // Half a record: exactly what a crash mid-append leaves behind.
            ScopedTerrainFault fault(Stage::PayloadAppend, 64);
            CHECK(fixture.saveDirty(dirty) == Result::Failed);
        }

        // Nothing on disk references the partial tail, so the file is still the previous
        // generation in full — header, index and every tile payload.
        TerrainFileSnapshot after;
        REQUIRE(readTerrainTestSnapshot(fixture.file.string(), after));
        REQUIRE(after.index.size() == beforeIndex.size());
        for (size_t i = 0; i < beforeIndex.size(); ++i)
            CHECK(sameTerrainTestEntry(beforeIndex[i], after.index[i]));
        CHECK(allTerrainTestTilesRead(fixture.file, after.index));
        CHECK(terrainTestFileSize(fixture.file) > beforeSize);
        CHECK_FALSE(terrain_test_fs::exists(terrainTestJournalPath(fixture.file)));

        // No journal was ever committed, so there is nothing to replay or roll back. Recovery
        // deliberately does not truncate the tail — that would be its only destructive act, and
        // the bytes are accounted as obsolete instead.
        CHECK(terrain::TerrainSerializer::recoverPending(fixture.file.string()) == Recovery::NotNeeded);
        CHECK(terrainTestFileSize(fixture.file) > beforeSize);
    }

    TEST_CASE("the tail leaked by an interrupted append is obsolete and compaction reclaims it")
    {
        IncrementalTerrainFixture fixture;
        const uint64_t beforeSize = terrainTestFileSize(fixture.file);
        CHECK(terrainTestOccupancy(fixture.snapshot, beforeSize).obsoleteBytes == 0);

        const TerrainTestDirtySet dirty = {dirtyOneTile(fixture, 0, 0.5f)};
        {
            ScopedTerrainFault fault(Stage::PayloadAppend, 96);
            CHECK(fixture.saveDirty(dirty) == Result::Failed);
        }

        fixture.refresh();
        const uint64_t leakedSize = terrainTestFileSize(fixture.file);
        const auto usage = terrainTestOccupancy(fixture.snapshot, leakedSize);
        CHECK(usage.consistent);
        CHECK(usage.obsoleteBytes == leakedSize - beforeSize);

        std::vector<DecodedTile> before;
        REQUIRE(decodeTiles(fixture.file, fixture.snapshot.index, before));

        REQUIRE(terrain::TerrainSerializer::compact(fixture.file.string()));

        fixture.refresh();
        const uint64_t compactedSize = terrainTestFileSize(fixture.file);
        const auto compactedUsage = terrainTestOccupancy(fixture.snapshot, compactedSize);
        CHECK(compactedUsage.obsoleteBytes == 0);
        CHECK(compactedSize == beforeSize);

        std::vector<DecodedTile> after;
        REQUIRE(decodeTiles(fixture.file, fixture.snapshot.index, after));
        CHECK(sameDecodedTiles(before, after));
    }

    TEST_CASE("an incomplete journal is discarded and the previous generation survives")
    {
        IncrementalTerrainFixture fixture;
        const auto beforeIndex = fixture.snapshot.index;

        const TerrainTestDirtySet dirty = {dirtyOneTile(fixture, 1, 0.25f)};
        {
            // 96 bytes is past the journal's 56-byte prefix but far short of its trailer, so the
            // commit hash is missing entirely.
            ScopedTerrainFault fault(Stage::JournalWrite, 96);
            CHECK(fixture.saveDirty(dirty) == Result::Failed);
        }

        CHECK(terrain_test_fs::exists(terrainTestJournalPath(fixture.file)));
        CHECK(terrain::TerrainSerializer::recoverPending(fixture.file.string()) == Recovery::Discarded);
        CHECK_FALSE(terrain_test_fs::exists(terrainTestJournalPath(fixture.file)));

        TerrainFileSnapshot after;
        REQUIRE(readTerrainTestSnapshot(fixture.file.string(), after));
        REQUIRE(after.index.size() == beforeIndex.size());
        for (size_t i = 0; i < beforeIndex.size(); ++i)
            CHECK(sameTerrainTestEntry(beforeIndex[i], after.index[i]));
        CHECK(allTerrainTestTilesRead(fixture.file, after.index));
    }

    TEST_CASE("a committed journal is replayed and publishes the new generation")
    {
        IncrementalTerrainFixture fixture;
        const auto beforeIndex = fixture.snapshot.index;
        const auto dirtyCoord = dirtyOneTile(fixture, 1, 0.75f);
        const TerrainTestDirtySet dirty = {dirtyCoord};

        {
            ScopedTerrainFault fault(Stage::AfterJournalCommit);
            CHECK(fixture.saveDirty(dirty) == Result::Failed);
        }

        // The commit is durable in the journal but has not reached the file yet, so the file still
        // reads as the previous generation.
        TerrainFileSnapshot beforeReplay;
        REQUIRE(readTerrainTestSnapshot(fixture.file.string(), beforeReplay));
        const auto* stale = findTerrainTestEntry(beforeReplay.index, dirtyCoord);
        const auto* original = findTerrainTestEntry(beforeIndex, dirtyCoord);
        REQUIRE(stale != nullptr);
        REQUIRE(original != nullptr);
        CHECK(sameTerrainTestEntry(*stale, *original));

        CHECK(terrain::TerrainSerializer::recoverPending(fixture.file.string()) == Recovery::Redone);
        CHECK_FALSE(terrain_test_fs::exists(terrainTestJournalPath(fixture.file)));

        fixture.refresh();
        const auto* replayed = findTerrainTestEntry(fixture.snapshot.index, dirtyCoord);
        REQUIRE(replayed != nullptr);
        CHECK(replayed->heightDataOffset > original->heightDataOffset);
        CHECK(allTerrainTestTilesRead(fixture.file, fixture.snapshot.index));

        // The edit that was in flight is the one now on disk.
        std::vector<float> heights;
        REQUIRE(terrain::TerrainSerializer::readTileHeights(fixture.file.string(), *replayed, heights));
        const terrain::TerrainTile* tile = fixture.grid->getTile(dirtyCoord);
        REQUIRE(tile != nullptr);
        REQUIRE(heights.size() == tile->heightData.size());

        // Every tile that was not dirty keeps its entry exactly.
        for (const auto& coord : {fixture.coords[0], fixture.coords[2]})
        {
            const auto* was = findTerrainTestEntry(beforeIndex, coord);
            const auto* is = findTerrainTestEntry(fixture.snapshot.index, coord);
            REQUIRE(was != nullptr);
            REQUIRE(is != nullptr);
            CHECK(sameTerrainTestEntry(*was, *is));
        }
    }

    TEST_CASE("a header commit interrupted part-way is completed by replay")
    {
        IncrementalTerrainFixture fixture;
        // An equal-length material path is the only header field an incremental save may change,
        // so it is what makes a partial header write observable at all.
        const std::string newPath(fixture.materialPath.size(), 'z');
        const TerrainTestDirtySet dirty = {dirtyOneTile(fixture, 1, 0.125f)};

        {
            // 40 bytes lands inside the fixed prefix, well before the material path at offset 85.
            ScopedTerrainFault fault(Stage::HeaderCommit, 40);
            CHECK(fixture.saveDirty(dirty, nullptr, nullptr, &newPath) == Result::Failed);
        }

        TerrainFileSnapshot torn;
        REQUIRE(readTerrainTestSnapshot(fixture.file.string(), torn));
        CHECK(torn.header.materialPath == fixture.materialPath);   // the new path never landed

        CHECK(terrain::TerrainSerializer::recoverPending(fixture.file.string()) == Recovery::Redone);

        fixture.refresh();
        CHECK(fixture.snapshot.header.materialPath == newPath);
        CHECK(terrain::serializedHeaderSize(fixture.snapshot.header) ==
              fixture.snapshot.indexTableOffset);
        CHECK(allTerrainTestTilesRead(fixture.file, fixture.snapshot.index));
    }

    TEST_CASE("an index commit torn mid-entry is repaired by replay")
    {
        IncrementalTerrainFixture fixture;
        const auto beforeIndexBytes = indexRegionBytes(fixture.file, fixture.snapshot);
        REQUIRE_FALSE(beforeIndexBytes.empty());

        const auto dirtyCoord = dirtyOneTile(fixture, 1, 0.5f);
        const TerrainTestDirtySet dirty = {dirtyCoord};

        {
            // Stop 13 bytes into the second entry: its coordinates are rewritten and its
            // heightDataOffset is left half old, half new. This is the state that corrupts a
            // terrain silently today, because nothing in the format can detect it.
            ScopedTerrainFault fault(Stage::IndexCommit, terrain::TILE_INDEX_ENTRY_SIZE + 13);
            CHECK(fixture.saveDirty(dirty) == Result::Failed);
        }

        const auto tornIndexBytes = indexRegionBytes(fixture.file, fixture.snapshot);
        REQUIRE_FALSE(tornIndexBytes.empty());
        CHECK(tornIndexBytes != beforeIndexBytes);   // the tear is real

        CHECK(terrain::TerrainSerializer::recoverPending(fixture.file.string()) == Recovery::Redone);

        fixture.refresh();
        CHECK(allTerrainTestTilesRead(fixture.file, fixture.snapshot.index));
        const auto* repaired = findTerrainTestEntry(fixture.snapshot.index, dirtyCoord);
        REQUIRE(repaired != nullptr);
        CHECK(repaired->payloadSize > 0);
    }

    TEST_CASE("replay is idempotent when the commit already reached the file")
    {
        IncrementalTerrainFixture fixture;
        const TerrainTestDirtySet dirty = {dirtyOneTile(fixture, 0, 0.25f)};

        {
            // The patch is durable; only the journal's removal was interrupted.
            ScopedTerrainFault fault(Stage::BeforeJournalDelete);
            CHECK(fixture.saveDirty(dirty) == Result::Failed);
        }

        CHECK(terrain_test_fs::exists(terrainTestJournalPath(fixture.file)));
        CHECK(terrain::TerrainSerializer::recoverPending(fixture.file.string()) == Recovery::Redone);
        const auto afterFirst = readTerrainTestFileBytes(fixture.file.path());

        CHECK(terrain::TerrainSerializer::recoverPending(fixture.file.string()) == Recovery::NotNeeded);
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == afterFirst);
        CHECK(allTerrainTestTilesRead(fixture.file, fixture.snapshot.index));
    }

    TEST_CASE("a journal that does not match the file it targets is discarded")
    {
        IncrementalTerrainFixture fixture;
        const TerrainTestDirtySet dirty = {dirtyOneTile(fixture, 1, 0.25f)};

        {
            ScopedTerrainFault fault(Stage::AfterJournalCommit);
            CHECK(fixture.saveDirty(dirty) == Result::Failed);
        }

        // Stand in for "something else replaced this file since the journal was written". Replaying
        // a stale commit onto a different generation is the corruption the size check prevents.
        std::error_code ec;
        terrain_test_fs::resize_file(fixture.file.path(), terrainTestFileSize(fixture.file) - 1, ec);
        REQUIRE_FALSE(ec);

        CHECK(terrain::TerrainSerializer::recoverPending(fixture.file.string()) == Recovery::Discarded);
        CHECK_FALSE(terrain_test_fs::exists(terrainTestJournalPath(fixture.file)));
    }

    TEST_CASE("a full save interrupted before its replacement leaves the destination complete")
    {
        IncrementalTerrainFixture fixture;
        const auto before = readTerrainTestFileBytes(fixture.file.path());

        terrain::TerrainTile* tile = fixture.grid->getTile(fixture.coords[0]);
        REQUIRE(tile != nullptr);
        tile->heightData[0] += 1.5f;

        {
            ScopedTerrainFault fault(Stage::BeforeReplace);
            CHECK_FALSE(terrain::TerrainSerializer::save(
                makeTerrainTestSaveParams(fixture.file.string(), *fixture.grid, fixture.config,
                                          fixture.materialPath)));
        }

        // The destination was never unlinked, so it is byte-for-byte the previous save.
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);
        CHECK(allTerrainTestTilesRead(fixture.file, fixture.snapshot.index));
        CHECK(terrain_test_fs::exists(terrainTestTempPath(fixture.file)));

        CHECK(terrain::TerrainSerializer::recoverPending(fixture.file.string()) == Recovery::Discarded);
        CHECK_FALSE(terrain_test_fs::exists(terrainTestTempPath(fixture.file)));
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);
    }

    TEST_CASE("a first full save interrupted before its replacement creates no partial destination")
    {
        const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
        auto grid = makePopulatedTerrainTestGrid(config, {{0, 0}, {1, 0}});
        ScopedTerrainTestFile file("first-save");

        {
            ScopedTerrainFault fault(Stage::BeforeReplace);
            CHECK_FALSE(terrain::TerrainSerializer::save(
                makeTerrainTestSaveParams(file.string(), *grid, config)));
        }

        // Nothing was promoted, so there is no half-written .vfTerrain for anything to open.
        CHECK_FALSE(terrain_test_fs::exists(file.path()));
        CHECK(terrain_test_fs::exists(terrainTestTempPath(file)));
        CHECK(terrain::TerrainSerializer::recoverPending(file.string()) == Recovery::Discarded);
        CHECK_FALSE(terrain_test_fs::exists(terrainTestTempPath(file)));
    }

    TEST_CASE("an interrupted compaction leaves the source file untouched")
    {
        IncrementalTerrainFixture fixture;
        const TerrainTestDirtySet dirty = {dirtyOneTile(fixture, 1, 0.25f)};
        REQUIRE(fixture.saveDirtySucceeds(dirty));
        fixture.refresh();

        const auto before = readTerrainTestFileBytes(fixture.file.path());
        {
            ScopedTerrainFault fault(Stage::CompactionCopy, 32);
            CHECK_FALSE(terrain::TerrainSerializer::compact(fixture.file.string()));
        }

        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);
        CHECK(allTerrainTestTilesRead(fixture.file, fixture.snapshot.index));
    }

    TEST_CASE("compaction preserves every tile, the header and every feature flag")
    {
        IncrementalTerrainFixture fixture;

        for (uint32_t pass = 0; pass < 3; ++pass)
        {
            const TerrainTestDirtySet dirty = {
                dirtyOneTile(fixture, pass % fixture.coords.size(), 0.125f)};
            REQUIRE(fixture.saveDirtySucceeds(dirty));
            fixture.refresh();
        }

        const auto beforeHeader = fixture.snapshot.header;
        const uint64_t beforeIndexOffset = fixture.snapshot.indexTableOffset;
        std::vector<DecodedTile> before;
        REQUIRE(decodeTiles(fixture.file, fixture.snapshot.index, before));
        const auto beforeUsage = terrainTestOccupancy(fixture.snapshot, terrainTestFileSize(fixture.file));
        CHECK(beforeUsage.obsoleteBytes > 0);

        REQUIRE(terrain::TerrainSerializer::compact(fixture.file.string()));
        fixture.refresh();

        const auto afterUsage = terrainTestOccupancy(fixture.snapshot, terrainTestFileSize(fixture.file));
        CHECK(afterUsage.obsoleteBytes == 0);
        CHECK(afterUsage.liveBytes == beforeUsage.liveBytes);

        std::vector<DecodedTile> after;
        REQUIRE(decodeTiles(fixture.file, fixture.snapshot.index, after));
        CHECK(sameDecodedTiles(before, after));

        CHECK(static_cast<uint32_t>(fixture.snapshot.header.flags) ==
              static_cast<uint32_t>(beforeHeader.flags));
        CHECK(fixture.snapshot.header.materialPath == beforeHeader.materialPath);
        CHECK(fixture.snapshot.header.tileCount == beforeHeader.tileCount);
        CHECK(fixture.snapshot.header.physicsConfig.hasCollider ==
              beforeHeader.physicsConfig.hasCollider);
        CHECK(fixture.snapshot.header.physicsConfig.friction == beforeHeader.physicsConfig.friction);
        CHECK(fixture.snapshot.header.streamingConfig.enabled ==
              beforeHeader.streamingConfig.enabled);
        CHECK(fixture.snapshot.header.streamingConfig.loadRadius ==
              beforeHeader.streamingConfig.loadRadius);
        CHECK(fixture.snapshot.indexTableOffset == beforeIndexOffset);
    }

    TEST_CASE("compaction is skipped below the documented threshold")
    {
        IncrementalTerrainFixture fixture;
        const TerrainTestDirtySet dirty = {dirtyOneTile(fixture, 1, 0.25f)};
        REQUIRE(fixture.saveDirtySucceeds(dirty));
        fixture.refresh();

        const auto usage = terrainTestOccupancy(fixture.snapshot, terrainTestFileSize(fixture.file));
        REQUIRE(usage.obsoleteBytes > 0);
        REQUIRE(usage.obsoleteBytes < terrain::TERRAIN_COMPACT_MIN_OBSOLETE_BYTES);

        const auto before = readTerrainTestFileBytes(fixture.file.path());
        bool compacted = true;
        CHECK(terrain::TerrainSerializer::compactIfNeeded(fixture.file.string(), &compacted));
        CHECK_FALSE(compacted);
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);
    }

    TEST_CASE("the compaction threshold fires at its documented boundaries")
    {
        const auto usage = [](uint64_t live, uint64_t obsolete)
        {
            terrain::TerrainFileOccupancy value;
            value.liveBytes = live;
            value.obsoleteBytes = obsolete;
            value.consistent = true;
            return value;
        };

        // Below the absolute floor nothing compacts, however lopsided the ratio.
        CHECK_FALSE(terrain::shouldCompactTerrainFile(
            usage(1024, terrain::TERRAIN_COMPACT_MIN_OBSOLETE_BYTES - 1)));
        // At the floor, a quarter of the live bytes is enough.
        CHECK(terrain::shouldCompactTerrainFile(
            usage(terrain::TERRAIN_COMPACT_MIN_OBSOLETE_BYTES * 4,
                  terrain::TERRAIN_COMPACT_MIN_OBSOLETE_BYTES)));
        // A large file with a small ratio waits...
        CHECK_FALSE(terrain::shouldCompactTerrainFile(usage(1024ull << 20, 32ull << 20)));
        // ...until the absolute cap catches it.
        CHECK(terrain::shouldCompactTerrainFile(
            usage(1024ull << 20, terrain::TERRAIN_COMPACT_ABSOLUTE_BYTES)));
        // An inconsistent file never compacts — the arithmetic under it is not trustworthy.
        terrain::TerrainFileOccupancy broken = usage(1024, 1024ull << 20);
        broken.consistent = false;
        CHECK_FALSE(terrain::shouldCompactTerrainFile(broken));
    }

    TEST_CASE("recovery and compaction are refused for a terrain inside an archive")
    {
        IncrementalTerrainFixture fixture;
        const auto before = readTerrainTestFileBytes(fixture.file.path());

        ScopedArchiveMode archive;
        CHECK(terrain::TerrainSerializer::recoverPending(fixture.file.string()) == Recovery::NotNeeded);
        CHECK_FALSE(terrain::TerrainSerializer::compact(fixture.file.string()));
        CHECK(terrain::TerrainSerializer::compactIfNeeded(fixture.file.string()));
        CHECK(readTerrainTestFileBytes(fixture.file.path()) == before);
    }
}
