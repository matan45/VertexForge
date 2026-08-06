// VK-1646 — the two-file commit, seen from the serializer.
//
// A terrain and its `.vfterrainlayers` sidecar are written as one logical save but committed as
// two renames, because no filesystem offers an atomic swap of a pair. What makes that safe is not
// the ordering alone but the generation id: whichever rename the process died between, the
// surviving pair either matches or provably does not.
//
// These cases drive real saves through the fault injector and assert on the bytes that survive.
// The format itself is covered in test_terrain_layer_sidecar.cpp.

#include <doctest.h>

#include "test_terrain_serializer_fixture.hpp"

#include <terrain/TerrainHeightLayerStore.hpp>
#include <terrain/TerrainLayerSidecar.hpp>

#include <asset/AssetReferenceScanner.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    // Whole-file compare, so "left alone" means byte-identical rather than merely still present.
    std::vector<uint8_t> readTerrainLayerSidecarBytes(const terrain_test_fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                    std::istreambuf_iterator<char>());
    }

    terrain::HeightLayerRecord makeRecoveryTestLayer(uint64_t id,
                                                     const std::vector<terrain::TileCoord>& affected)
    {
        terrain::HeightLayerRecord record;
        record.id = id;
        record.visible = true;
        record.type = terrain::HeightLayerType::SplineCorridor;
        record.spline.corridor.corridorWidth = 6.0f;
        record.spline.corridor.falloffWidth = 3.0f;
        record.spline.corridor.embankmentHeight = 0.5f;
        record.spline.samples = {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(48.0f, 1.0f, 16.0f)};
        for (const terrain::TileCoord& coord : affected)
            record.affected.insert(coord);
        record.eval = terrain::makeSplineCorridorEval(record.spline);
        return record;
    }

    // Seeds a grid's store the way a spline apply would: adopt each covered tile's live plane as
    // its authoritative base, then register the layer over them.
    void seedGridLayers(terrain::TerrainGrid& grid,
                        const std::vector<terrain::TileCoord>& coords)
    {
        terrain::TerrainHeightLayerStore& store = grid.getHeightLayers();
        for (const terrain::TileCoord& coord : coords)
        {
            terrain::TerrainTile* tile = grid.getTile(coord);
            REQUIRE(tile != nullptr);
            store.adoptBase(coord, tile->heightData, tile->config.getVertexCount());
        }
        store.addLayer(makeRecoveryTestLayer(1, coords));
    }

    bool terrainClaimsSidecar(const std::string& path)
    {
        TerrainFileSnapshot snapshot;
        REQUIRE(readTerrainTestSnapshot(path, snapshot));
        return terrain::hasFlag(snapshot.header.flags,
                                terrain::TerrainFormatFlags::HAS_EDIT_LAYER_SIDECAR);
    }

    // Straight out of the terrain's own header — the id is stored, not derived, which is the
    // whole point of it surviving compaction and a material rename.
    uint64_t liveGenerationId(const std::string& path)
    {
        TerrainFileSnapshot snapshot;
        REQUIRE(readTerrainTestSnapshot(path, snapshot));
        return snapshot.header.editLayerGenerationId;
    }

    terrain::TerrainLayerSidecarStatus readSidecarBesideTerrain(const ScopedTerrainTestFile& file,
                                                               terrain::TerrainHeightLayerStore& out)
    {
        terrain::TerrainLayerSidecarMeta meta;
        return terrain::readTerrainLayerSidecar(terrain::terrainLayerSidecarPath(file.path()),
                                                liveGenerationId(file.string()), meta, out);
    }
}

TEST_CASE("A full save commits the terrain and its edit-layer sidecar together")
{
    const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
    const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}, {2, 0}};
    auto grid = makePopulatedTerrainTestGrid(config, coords, true);
    seedGridLayers(*grid, {{0, 0}, {1, 0}});

    ScopedTerrainTestFile file("layers-full-save");
    auto params = makeTerrainTestSaveParams(file.string(), *grid, config);
    params.heightLayers = &grid->getHeightLayers();
    params.terrainGuid = 0xFEEDFACECAFEBEEFull;

    REQUIRE(terrain::TerrainSerializer::save(params));

    SUBCASE("the marker is set and the sidecar is there")
    {
        CHECK(terrainClaimsSidecar(file.string()));
        CHECK(terrain_test_fs::exists(terrain::terrainLayerSidecarPath(file.path())));
    }

    SUBCASE("the sidecar names the generation that was actually committed")
    {
        // One id, generated once and stamped into both files before either rename, so the value in
        // the sidecar has to match the terrain that ended up in place — that equality is the
        // entire binding.
        terrain::TerrainHeightLayerStore loaded;
        REQUIRE(readSidecarBesideTerrain(file, loaded) == terrain::TerrainLayerSidecarStatus::Ok);
        CHECK(loaded.layers().size() == 1);
        CHECK(loaded.baseCount() == 2);
    }

    SUBCASE("the bases round-trip through the sidecar, not through the terrain's quantisation")
    {
        // The point of the whole ticket: VFTR stores derived heights as quantised uint16, so a
        // base that travelled through it would come back subtly different every save.
        terrain::TerrainHeightLayerStore loaded;
        REQUIRE(readSidecarBesideTerrain(file, loaded) == terrain::TerrainLayerSidecarStatus::Ok);
        for (const auto& entry : grid->getHeightLayers().allBases())
        {
            const terrain::BaseHeightBlock* block = loaded.base(entry.first);
            REQUIRE(block != nullptr);
            CHECK(block->heights == entry.second.heights);
        }
    }

    SUBCASE("no temporaries are left behind")
    {
        CHECK_FALSE(terrain_test_fs::exists(terrainTestTempPath(file)));
        terrain_test_fs::path sidecarTmp = terrain::terrainLayerSidecarPath(file.path());
        sidecarTmp += ".tmp";
        CHECK_FALSE(terrain_test_fs::exists(sidecarTmp));
    }
}

TEST_CASE("A terrain with no authoring state neither claims nor keeps a sidecar")
{
    const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
    const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}};
    auto grid = makePopulatedTerrainTestGrid(config, coords, true);

    ScopedTerrainTestFile file("layers-none");
    auto params = makeTerrainTestSaveParams(file.string(), *grid, config);
    params.heightLayers = &grid->getHeightLayers();

    REQUIRE(terrain::TerrainSerializer::save(params));
    CHECK_FALSE(terrainClaimsSidecar(file.string()));
    CHECK_FALSE(terrain_test_fs::exists(terrain::terrainLayerSidecarPath(file.path())));

    SUBCASE("emptying the stack removes the sidecar the previous save wrote")
    {
        seedGridLayers(*grid, {{0, 0}});
        REQUIRE(terrain::TerrainSerializer::save(params));
        REQUIRE(terrain_test_fs::exists(terrain::terrainLayerSidecarPath(file.path())));
        REQUIRE(terrainClaimsSidecar(file.string()));

        // Deliberate deletion, not eviction: the base and the layer are both gone, so leaving the
        // file would strand an orphan the next load has to warn about forever.
        grid->getHeightLayers().removeLayer(1);
        grid->getHeightLayers().eraseBase({0, 0});
        REQUIRE(grid->getHeightLayers().empty());

        REQUIRE(terrain::TerrainSerializer::save(params));
        CHECK_FALSE(terrainClaimsSidecar(file.string()));
        CHECK_FALSE(terrain_test_fs::exists(terrain::terrainLayerSidecarPath(file.path())));
    }
}

// VK-1648 regression, and the reason TerrainService::persistableHeightLayers exists.
//
// The serializer reads a NON-NULL store as authoritative, so an empty one means "the stack was
// deliberately cleared -- delete the sidecar". A store left empty by a FAILED sidecar load is not
// that: its emptiness means "unknown". Handing it over answered a read error by destroying the
// file that caused it -- and because this branch bumps VFTL, every existing sidecar hits
// VersionMismatch at once, so the first re-save after an update was the data loss.
//
// The distinction below is what the service's guard relies on. Collapsing these two cases into one
// (treating nullptr like empty, "for simplicity") silently re-arms the bug.
TEST_CASE("A save that carries no store leaves an existing sidecar alone")
{
    const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
    const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}};
    auto grid = makePopulatedTerrainTestGrid(config, coords, true);
    seedGridLayers(*grid, {{0, 0}});

    ScopedTerrainTestFile file("layers-withheld-store");
    const auto sidecarPath = terrain::terrainLayerSidecarPath(file.path());

    auto params = makeTerrainTestSaveParams(file.string(), *grid, config);
    params.heightLayers = &grid->getHeightLayers();
    params.terrainGuid = 0x1648164816481648ull;

    REQUIRE(terrain::TerrainSerializer::save(params));
    REQUIRE(terrain_test_fs::exists(sidecarPath));
    const auto committed = readTerrainLayerSidecarBytes(sidecarPath);
    REQUIRE_FALSE(committed.empty());

    SUBCASE("nullptr preserves the file, byte for byte")
    {
        // What TerrainService now passes when the store is editingLocked. The terrain is saved,
        // bit 6 is cleared so the unreadable stack is not applied over freshly sculpted ground,
        // and the artist's file is still on disk to repair or restore from.
        params.heightLayers = nullptr;
        REQUIRE(terrain::TerrainSerializer::save(params));

        CHECK_FALSE(terrainClaimsSidecar(file.string()));
        REQUIRE(terrain_test_fs::exists(sidecarPath));
        CHECK(readTerrainLayerSidecarBytes(sidecarPath) == committed);
    }

    SUBCASE("an empty store still deletes it, which is why the two must stay distinct")
    {
        terrain::TerrainHeightLayerStore emptied;
        REQUIRE(emptied.empty());
        params.heightLayers = &emptied;
        REQUIRE(terrain::TerrainSerializer::save(params));

        CHECK_FALSE(terrainClaimsSidecar(file.string()));
        CHECK_FALSE(terrain_test_fs::exists(sidecarPath));
    }
}

TEST_CASE("An interrupted two-file commit is always resolvable")
{
    const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
    const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}};
    auto grid = makePopulatedTerrainTestGrid(config, coords, true);
    seedGridLayers(*grid, {{0, 0}});

    ScopedTerrainTestFile file("layers-interrupted");
    auto params = makeTerrainTestSaveParams(file.string(), *grid, config);
    params.heightLayers = &grid->getHeightLayers();
    params.terrainGuid = 7;

    REQUIRE(terrain::TerrainSerializer::save(params));
    const uint64_t firstGeneration = liveGenerationId(file.string());

    SUBCASE("dying between the two renames leaves a terrain whose sidecar is provably stale")
    {
        // Change the terrain so the second save produces different bytes, then stop right after
        // the terrain has been committed and before the sidecar has.
        terrain::TerrainTile* tile = grid->getTile({0, 0});
        REQUIRE(tile != nullptr);
        tile->heightData[0] += 12.5f;

        {
            ScopedTerrainFault fault(terrain::TerrainSaveStage::BetweenTerrainAndSidecarReplace);
            // The terrain IS saved at this point — reporting failure would leave the caller holding
            // an index built for the file that was just replaced.
            CHECK(terrain::TerrainSerializer::save(params));
        }

        const uint64_t secondGeneration = liveGenerationId(file.string());
        CHECK(secondGeneration != firstGeneration);

        // The terrain is whole and readable: nothing about a sidecar failure touches it.
        TerrainFileSnapshot snapshot;
        REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
        CHECK(allTerrainTestTilesRead(file, snapshot.index));

        // And the surviving sidecar names the previous generation, so it is refused rather than
        // composed over ground it was never authored against.
        terrain::TerrainHeightLayerStore loaded;
        CHECK(readSidecarBesideTerrain(file, loaded) == terrain::TerrainLayerSidecarStatus::Stale);
        CHECK(loaded.empty());
    }

    SUBCASE("a torn sidecar write commits neither file")
    {
        terrain::TerrainTile* tile = grid->getTile({0, 0});
        REQUIRE(tile != nullptr);
        tile->heightData[0] += 3.0f;

        {
            ScopedTerrainFault fault(terrain::TerrainSaveStage::SidecarWrite, 64);
            CHECK_FALSE(terrain::TerrainSerializer::save(params));
        }

        // Both temporaries were abandoned, so the previous generation is still what is on disk —
        // strictly better than a saved terrain whose authoring data was silently dropped.
        CHECK(liveGenerationId(file.string()) == firstGeneration);
        terrain::TerrainHeightLayerStore loaded;
        CHECK(readSidecarBesideTerrain(file, loaded) == terrain::TerrainLayerSidecarStatus::Ok);
    }

    SUBCASE("a leftover sidecar temporary is swept, never promoted")
    {
        terrain_test_fs::path sidecarTmp = terrain::terrainLayerSidecarPath(file.path());
        sidecarTmp += ".tmp";
        {
            std::ofstream stranded(sidecarTmp, std::ios::binary | std::ios::trunc);
            stranded << "half a sidecar";
        }
        REQUIRE(terrain_test_fs::exists(sidecarTmp));

        // A temp only exists between "both written" and "both committed". Whichever side the crash
        // fell on it describes a generation nothing is guaranteed to be, so recovery drops it.
        CHECK(terrain::TerrainSerializer::recoverPending(file.string()) !=
              terrain::TerrainRecoveryResult::Failed);
        CHECK_FALSE(terrain_test_fs::exists(sidecarTmp));
    }
}

TEST_CASE("A missing sidecar degrades the terrain instead of failing it")
{
    const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
    const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}};
    auto grid = makePopulatedTerrainTestGrid(config, coords, true);
    seedGridLayers(*grid, {{0, 0}, {1, 0}});

    ScopedTerrainTestFile file("layers-missing");
    auto params = makeTerrainTestSaveParams(file.string(), *grid, config);
    params.heightLayers = &grid->getHeightLayers();
    REQUIRE(terrain::TerrainSerializer::save(params));

    terrain_test_fs::remove(terrain::terrainLayerSidecarPath(file.path()));

    // The marker still promises a sidecar — that mismatch is exactly what makes the situation
    // reportable rather than invisible.
    CHECK(terrainClaimsSidecar(file.string()));

    TerrainFileSnapshot snapshot;
    REQUIRE(readTerrainTestSnapshot(file.string(), snapshot));
    CHECK(allTerrainTestTilesRead(file, snapshot.index));

    terrain::TerrainHeightLayerStore loaded;
    CHECK(readSidecarBesideTerrain(file, loaded) == terrain::TerrainLayerSidecarStatus::Absent);
    CHECK(loaded.empty());
}

TEST_CASE("A save by a build that drops the store orphans the sidecar rather than corrupting it")
{
    const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
    const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}};
    auto grid = makePopulatedTerrainTestGrid(config, coords, true);
    seedGridLayers(*grid, {{0, 0}});

    ScopedTerrainTestFile file("layers-orphan");
    auto withLayers = makeTerrainTestSaveParams(file.string(), *grid, config);
    withLayers.heightLayers = &grid->getHeightLayers();
    REQUIRE(terrain::TerrainSerializer::save(withLayers));
    REQUIRE(terrainClaimsSidecar(file.string()));

    // Stands in for an older editor, or any path that saves without carrying authoring state.
    auto withoutLayers = makeTerrainTestSaveParams(file.string(), *grid, config);
    REQUIRE(terrain::TerrainSerializer::save(withoutLayers));

    // Bit 6 is now clear, so the file beside the terrain is an orphan: never applied, and never
    // deleted either — a load path has no business destroying data it cannot identify.
    CHECK_FALSE(terrainClaimsSidecar(file.string()));
    CHECK(terrain_test_fs::exists(terrain::terrainLayerSidecarPath(file.path())));

    terrain::TerrainLayerSidecarMeta meta;
    CHECK(terrain::peekTerrainLayerSidecar(terrain::terrainLayerSidecarPath(file.path()), meta) ==
          terrain::TerrainLayerSidecarStatus::Ok);
}

TEST_CASE("Gaining a first layer costs one full save; keeping it stays incremental")
{
    IncrementalTerrainFixture fixture;

    // Baseline: no authoring state, incremental works as VK-1644 left it.
    REQUIRE(fixture.saveDirtySucceeds({{0, 0}}));
    fixture.refresh();
    CHECK_FALSE(terrain::hasFlag(fixture.snapshot.header.flags,
                                 terrain::TerrainFormatFlags::HAS_EDIT_LAYER_SIDECAR));

    seedGridLayers(*fixture.grid, {{0, 0}});

    // Bit 6 gates an 8-byte header block, so turning it on moves the index table — and an
    // incremental save patches the header in place at a cached offset. Refusing is the only sound
    // answer, and NeedsFullSave (not Failed) is what makes the service's fallback safe.
    CHECK(fixture.saveDirty({{1, 0}}) == terrain::TerrainIncrementalSaveResult::NeedsFullSave);

    // Perform that one full save, the way the service would.
    auto fullParams = makeTerrainTestSaveParams(fixture.file.string(), *fixture.grid,
                                                fixture.config, fixture.materialPath);
    fullParams.heightLayers = &fixture.grid->getHeightLayers();
    REQUIRE(terrain::TerrainSerializer::save(fullParams));
    fixture.refresh();

    REQUIRE(terrain::hasFlag(fixture.snapshot.header.flags,
                             terrain::TerrainFormatFlags::HAS_EDIT_LAYER_SIDECAR));
    const uint64_t afterFullSave = fixture.snapshot.header.editLayerGenerationId;
    CHECK(afterFullSave != 0);

    SUBCASE("subsequent saves stay incremental — the block is already there, only its value moves")
    {
        // This is what the id buys over a content hash: a hash would have to be recomputed from the
        // whole file, which is the one thing an incremental save exists not to do.
        CHECK(fixture.saveDirty({{1, 0}}) == terrain::TerrainIncrementalSaveResult::Success);
        fixture.refresh();

        CHECK(terrain::hasFlag(fixture.snapshot.header.flags,
                               terrain::TerrainFormatFlags::HAS_EDIT_LAYER_SIDECAR));

        // Re-stamped, because the sidecar was rewritten alongside — so a copy of the OLD sidecar
        // can never pass as current.
        const uint64_t afterIncremental = fixture.snapshot.header.editLayerGenerationId;
        CHECK(afterIncremental != afterFullSave);

        terrain::TerrainLayerSidecarMeta meta;
        terrain::TerrainHeightLayerStore loaded;
        CHECK(terrain::readTerrainLayerSidecar(
                  terrain::terrainLayerSidecarPath(fixture.file.path()), afterIncremental, meta,
                  loaded) == terrain::TerrainLayerSidecarStatus::Ok);
        CHECK(loaded.layers().size() == 1);
    }

    SUBCASE("losing the last layer costs one full save too")
    {
        fixture.grid->getHeightLayers().removeLayer(1);
        fixture.grid->getHeightLayers().eraseBase({0, 0});
        REQUIRE(fixture.grid->getHeightLayers().empty());

        // The block has to come back out of the header, which again moves the index table.
        CHECK(fixture.saveDirty({{1, 0}}) == terrain::TerrainIncrementalSaveResult::NeedsFullSave);
    }
}

TEST_CASE("A terrain-material rename does not invalidate the layer sidecar")
{
    // The case that killed the content-hash design: AssetReferenceScanner::updateTerrainFile
    // rewrites the whole .vfterrain to splice in a renamed material path, shifting every payload
    // offset. Not one height changes, so the layer stack is still perfectly valid — but any binding
    // derived from the file's bytes would call it stale and cost the artist the entire stack.
    //
    // The header is copied through verbatim, so an id stored in it survives untouched.
    const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
    const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}};
    auto grid = makePopulatedTerrainTestGrid(config, coords, true);
    seedGridLayers(*grid, {{0, 0}});

    ScopedTerrainTestFile file("layers-material-rename");
    auto params = makeTerrainTestSaveParams(file.string(), *grid, config,
                                            "mat/before.vfTerrainMat");
    params.heightLayers = &grid->getHeightLayers();
    REQUIRE(terrain::TerrainSerializer::save(params));

    const uint64_t beforeRename = liveGenerationId(file.string());
    REQUIRE(beforeRename != 0);

    // A longer path, so the index table genuinely moves and every offset is rewritten.
    const auto searchRoot = file.path().parent_path().string();
    const auto renamed = asset::AssetReferenceScanner::updateReferences(
        "mat/before.vfTerrainMat", "mat/after-a-much-longer-name.vfTerrainMat", searchRoot);
    REQUIRE(renamed.updatedFiles.size() == 1);

    TerrainFileSnapshot rewritten;
    REQUIRE(readTerrainTestSnapshot(file.string(), rewritten));
    REQUIRE(rewritten.header.materialPath == "mat/after-a-much-longer-name.vfTerrainMat");
    // The index table really did move — otherwise this case would prove nothing.
    CHECK(terrain::serializedHeaderSize(rewritten.header) == rewritten.indexTableOffset);

    CHECK(liveGenerationId(file.string()) == beforeRename);

    terrain::TerrainHeightLayerStore loaded;
    CHECK(readSidecarBesideTerrain(file, loaded) == terrain::TerrainLayerSidecarStatus::Ok);
    CHECK(loaded.layers().size() == 1);
    CHECK(loaded.baseCount() == 1);
}

TEST_CASE("Compaction leaves the sidecar binding intact")
{
    const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
    const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}};
    auto grid = makePopulatedTerrainTestGrid(config, coords, true);
    seedGridLayers(*grid, {{0, 0}});

    ScopedTerrainTestFile file("layers-compaction");
    auto params = makeTerrainTestSaveParams(file.string(), *grid, config);
    params.heightLayers = &grid->getHeightLayers();
    REQUIRE(terrain::TerrainSerializer::save(params));

    terrain::TerrainHeightLayerStore before;
    REQUIRE(readSidecarBesideTerrain(file, before) == terrain::TerrainLayerSidecarStatus::Ok);
    const uint64_t beforeCompaction = liveGenerationId(file.string());

    // Compaction relocates every tile record and shifts every offset — a second byte-level rewrite
    // that changes no height. It re-emits the header verbatim, so the id rides through and the
    // sidecar stays bound with no repair step to get wrong.
    REQUIRE(terrain::TerrainSerializer::compact(file.string()));
    CHECK(liveGenerationId(file.string()) == beforeCompaction);

    terrain::TerrainHeightLayerStore after;
    CHECK(readSidecarBesideTerrain(file, after) == terrain::TerrainLayerSidecarStatus::Ok);
    CHECK(after.layers().size() == before.layers().size());
    CHECK(after.baseCount() == before.baseCount());
    for (const auto& entry : before.allBases())
    {
        const terrain::BaseHeightBlock* block = after.base(entry.first);
        REQUIRE(block != nullptr);
        CHECK(block->heights == entry.second.heights);
    }
}
