// VK-1646 — the `.vfterrainlayers` (VFTL) format itself.
//
// Pure format coverage: no TerrainGrid, no serializer, no service. Everything here builds a
// TerrainHeightLayerStore by hand, round-trips it, and then damages the bytes on disk in each of
// the ways the load contract has to tell apart. The serializer-level half — two-file commits,
// interrupted replacements, incremental refusal — lives in test_terrain_layer_sidecar_recovery.cpp.

#include <doctest.h>

#include "test_terrain_serializer_fixture.hpp"

#include <terrain/TerrainLayerSidecar.hpp>
#include <terrain/TerrainHeightLayerStore.hpp>

#include <resource/Crc32.hpp>
#include <resource/EndianUtils.hpp>

#include <cstring>
#include <fstream>
#include <vector>

namespace
{
    constexpr uint32_t SIDECAR_TEST_VERTS = 33; // Low resolution

    std::vector<float> makeSidecarTestHeights(uint32_t vertexCount, float bias)
    {
        std::vector<float> heights(static_cast<size_t>(vertexCount) * vertexCount);
        for (size_t i = 0; i < heights.size(); ++i)
            heights[i] = bias + static_cast<float>(i % 97) * 0.125f;
        return heights;
    }

    terrain::HeightLayerRecord makeSidecarTestLayer(uint64_t id, bool visible,
                                                    const std::vector<terrain::TileCoord>& affected)
    {
        terrain::HeightLayerRecord record;
        record.id = id;
        record.visible = visible;
        record.type = terrain::HeightLayerType::SplineCorridor;
        record.spline.corridor.corridorWidth = 4.5f + static_cast<float>(id);
        record.spline.corridor.falloffWidth = 2.25f;
        record.spline.corridor.embankmentHeight = -0.75f;
        record.spline.samples = {
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(16.0f, 1.5f, 8.0f),
            glm::vec3(32.0f, 2.0f, 24.0f),
        };
        for (const terrain::TileCoord& coord : affected)
            record.affected.insert(coord);
        record.eval = terrain::makeSplineCorridorEval(record.spline);
        return record;
    }

    // Two layers over three bases: enough that stack order, per-record parameters and the sparse
    // base index all have something to get wrong.
    void populateSidecarTestStore(terrain::TerrainHeightLayerStore& store)
    {
        store.addLayer(makeSidecarTestLayer(11, true, {{0, 0}, {1, 0}}));
        store.addLayer(makeSidecarTestLayer(22, false, {{1, 0}, {1, 1}}));

        store.adoptBase({0, 0}, makeSidecarTestHeights(SIDECAR_TEST_VERTS, 0.0f), SIDECAR_TEST_VERTS);
        store.adoptBase({1, 0}, makeSidecarTestHeights(SIDECAR_TEST_VERTS, 10.0f), SIDECAR_TEST_VERTS);
        store.adoptBase({1, 1}, makeSidecarTestHeights(SIDECAR_TEST_VERTS, 20.0f), SIDECAR_TEST_VERTS);
    }

    terrain::TerrainLayerSidecarMeta makeSidecarTestMeta(uint64_t generationId)
    {
        terrain::TerrainLayerSidecarMeta meta;
        meta.terrainGuid = 0xABCDEF0123456789ull;
        meta.generationId = generationId;
        meta.gridMinX = -1;
        meta.gridMinZ = -2;
        meta.gridMaxX = 3;
        meta.gridMaxZ = 4;
        meta.resolution = 0;
        meta.worldTileSize = 32.0f;
        return meta;
    }

    terrain_test_fs::path sidecarTestPath(const ScopedTerrainTestFile& file)
    {
        return terrain::terrainLayerSidecarPath(file.path());
    }

    std::vector<uint8_t> readSidecarBytes(const terrain_test_fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                    std::istreambuf_iterator<char>());
    }

    void writeSidecarBytes(const terrain_test_fs::path& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    // Recomputes the whole-file checksum after a test has rewritten bytes in place. Without this
    // every hand-edit would fail as Invalid, which would make it impossible to reach the reader's
    // later checks at all.
    void resealSidecar(std::vector<uint8_t>& bytes)
    {
        const size_t trailerOffset =
            bytes.size() - static_cast<size_t>(terrain::TERRAIN_LAYER_TRAILER_SIZE);
        const uint32_t crc =
            resource::endian::toLittleEndian(resource::crc32(bytes.data(), trailerOffset));
        std::memcpy(bytes.data() + trailerOffset, &crc, sizeof(uint32_t));
    }
}

TEST_CASE("VFTL round-trips a layer stack and its authoritative bases")
{
    ScopedTerrainTestFile file("sidecar-roundtrip");
    const auto path = sidecarTestPath(file);

    terrain::TerrainHeightLayerStore source;
    populateSidecarTestStore(source);

    const auto meta = makeSidecarTestMeta(0x1122334455667788ull);
    REQUIRE(terrain::writeTerrainLayerSidecar(path, meta, source));

    terrain::TerrainHeightLayerStore loaded;
    terrain::TerrainLayerSidecarMeta readMeta;
    REQUIRE(terrain::readTerrainLayerSidecar(path, meta.generationId, readMeta, loaded) ==
            terrain::TerrainLayerSidecarStatus::Ok);

    SUBCASE("metadata survives")
    {
        CHECK(readMeta.terrainGuid == meta.terrainGuid);
        CHECK(readMeta.generationId == meta.generationId);
        CHECK(readMeta.gridMinX == meta.gridMinX);
        CHECK(readMeta.gridMaxZ == meta.gridMaxZ);
        CHECK(readMeta.resolution == meta.resolution);
        CHECK(readMeta.worldTileSize == meta.worldTileSize);
    }

    SUBCASE("stack order, visibility and parameters survive")
    {
        REQUIRE(loaded.layers().size() == 2);

        // Order is the whole point of persisting a stack: composition is order-dependent, so a
        // reader that returned the same two layers the other way round would produce different
        // ground from the same file.
        CHECK(loaded.layers()[0].id == 11);
        CHECK(loaded.layers()[1].id == 22);

        CHECK(loaded.layers()[0].visible);
        CHECK_FALSE(loaded.layers()[1].visible);

        for (size_t i = 0; i < 2; ++i)
        {
            const auto& out = loaded.layers()[i];
            const auto& in = source.layers()[i];
            CHECK(out.type == terrain::HeightLayerType::SplineCorridor);
            CHECK(out.spline.corridor.corridorWidth == in.spline.corridor.corridorWidth);
            CHECK(out.spline.corridor.falloffWidth == in.spline.corridor.falloffWidth);
            CHECK(out.spline.corridor.embankmentHeight == in.spline.corridor.embankmentHeight);
            REQUIRE(out.spline.samples.size() == in.spline.samples.size());
            for (size_t s = 0; s < out.spline.samples.size(); ++s)
            {
                CHECK(out.spline.samples[s].x == in.spline.samples[s].x);
                CHECK(out.spline.samples[s].y == in.spline.samples[s].y);
                CHECK(out.spline.samples[s].z == in.spline.samples[s].z);
            }
        }
    }

    SUBCASE("affected sets survive exactly")
    {
        // Stored rather than recomputed from the polyline: the apply path only claims tiles that
        // were resident at the time, so re-deriving would widen a layer's reach across a reload.
        REQUIRE(loaded.layers().size() == 2);
        CHECK(loaded.layers()[0].affected == source.layers()[0].affected);
        CHECK(loaded.layers()[1].affected == source.layers()[1].affected);
    }

    SUBCASE("base blocks survive byte-for-byte")
    {
        REQUIRE(loaded.baseCount() == 3);
        for (const auto& entry : source.allBases())
        {
            const terrain::BaseHeightBlock* block = loaded.base(entry.first);
            REQUIRE(block != nullptr);
            CHECK(block->vertexCount == entry.second.vertexCount);
            CHECK(block->heights == entry.second.heights);
        }
    }

    SUBCASE("a loaded layer evaluates — the callable was rebuilt, not dropped")
    {
        REQUIRE(loaded.layers().size() == 2);
        CHECK(static_cast<bool>(loaded.layers()[0].eval));

        terrain::TerrainTileConfig config = makeTerrainTestConfig(terrain::TileResolution::Low);
        const std::vector<float> in = makeSidecarTestHeights(SIDECAR_TEST_VERTS, 5.0f);
        std::vector<float> out;
        loaded.layers()[0].eval({0, 0}, config, in, out);
        CHECK(out.size() == in.size());
    }

    SUBCASE("writing the same store twice is byte-identical")
    {
        // Both the affected set and the base index come out of unordered containers, so anything
        // that leaked hash order into the file would make two saves of one stack differ.
        ScopedTerrainTestFile second("sidecar-determinism");
        const auto secondPath = sidecarTestPath(second);
        REQUIRE(terrain::writeTerrainLayerSidecar(secondPath, meta, source));
        CHECK(readSidecarBytes(path) == readSidecarBytes(secondPath));
    }
}

TEST_CASE("VFTL reports every damaged state distinctly")
{
    ScopedTerrainTestFile file("sidecar-damage");
    const auto path = sidecarTestPath(file);

    terrain::TerrainHeightLayerStore source;
    populateSidecarTestStore(source);

    const uint64_t generation = 0x0F0F0F0F0F0F0F0Full;
    const auto meta = makeSidecarTestMeta(generation);
    REQUIRE(terrain::writeTerrainLayerSidecar(path, meta, source));

    const std::vector<uint8_t> pristine = readSidecarBytes(path);
    REQUIRE(pristine.size() > terrain::TERRAIN_LAYER_HEADER_SIZE);

    terrain::TerrainHeightLayerStore loaded;
    terrain::TerrainLayerSidecarMeta readMeta;

    SUBCASE("missing")
    {
        terrain_test_fs::remove(path);
        CHECK(terrain::readTerrainLayerSidecar(path, generation, readMeta, loaded) ==
              terrain::TerrainLayerSidecarStatus::Absent);
        CHECK(loaded.empty());
    }

    SUBCASE("stale — the file is sound, it just belongs to another generation")
    {
        CHECK(terrain::readTerrainLayerSidecar(path, generation ^ 1ull, readMeta, loaded) ==
              terrain::TerrainLayerSidecarStatus::Stale);
        // The header still parsed, so a diagnostic can name both generations.
        CHECK(readMeta.generationId == generation);
        CHECK(loaded.empty());
    }

    SUBCASE("truncated")
    {
        std::vector<uint8_t> truncated(
            pristine.begin(),
            pristine.begin() + static_cast<std::ptrdiff_t>(pristine.size() / 2));
        writeSidecarBytes(path, truncated);
        CHECK(terrain::readTerrainLayerSidecar(path, generation, readMeta, loaded) ==
              terrain::TerrainLayerSidecarStatus::Invalid);
        CHECK(loaded.empty());
    }

    SUBCASE("truncated to nothing but a header")
    {
        std::vector<uint8_t> stub(
            pristine.begin(),
            pristine.begin() + static_cast<std::ptrdiff_t>(terrain::TERRAIN_LAYER_HEADER_SIZE));
        writeSidecarBytes(path, stub);
        CHECK(terrain::readTerrainLayerSidecar(path, generation, readMeta, loaded) ==
              terrain::TerrainLayerSidecarStatus::Invalid);
    }

    SUBCASE("wrong magic")
    {
        std::vector<uint8_t> damaged = pristine;
        damaged[0] = 'X';
        resealSidecar(damaged);
        writeSidecarBytes(path, damaged);
        CHECK(terrain::readTerrainLayerSidecar(path, generation, readMeta, loaded) ==
              terrain::TerrainLayerSidecarStatus::Invalid);
    }

    SUBCASE("a future format version is refused rather than guessed at")
    {
        std::vector<uint8_t> damaged = pristine;
        const uint32_t future =
            resource::endian::toLittleEndian(terrain::TERRAIN_LAYER_VERSION_MAJOR + 1);
        std::memcpy(damaged.data() + 4, &future, sizeof(uint32_t));
        resealSidecar(damaged);
        writeSidecarBytes(path, damaged);
        CHECK(terrain::readTerrainLayerSidecar(path, generation, readMeta, loaded) ==
              terrain::TerrainLayerSidecarStatus::Invalid);
    }

    SUBCASE("a flipped bit inside a base block")
    {
        // Resealed, so the whole-file checksum passes and the per-block CRC is the only thing left
        // that can catch it — which is exactly why blocks carry their own.
        std::vector<uint8_t> damaged = pristine;
        damaged[damaged.size() - static_cast<size_t>(terrain::TERRAIN_LAYER_TRAILER_SIZE) - 8] ^= 0xFF;
        resealSidecar(damaged);
        writeSidecarBytes(path, damaged);
        CHECK(terrain::readTerrainLayerSidecar(path, generation, readMeta, loaded) ==
              terrain::TerrainLayerSidecarStatus::Invalid);
        CHECK(loaded.empty());
    }

    SUBCASE("a flipped bit inside a layer record")
    {
        std::vector<uint8_t> damaged = pristine;
        damaged[static_cast<size_t>(terrain::TERRAIN_LAYER_HEADER_SIZE) + 6] ^= 0xFF;
        resealSidecar(damaged);
        writeSidecarBytes(path, damaged);
        CHECK(terrain::readTerrainLayerSidecar(path, generation, readMeta, loaded) ==
              terrain::TerrainLayerSidecarStatus::Invalid);
    }

    SUBCASE("the commit magic must be present — a torn tail is not a short file")
    {
        std::vector<uint8_t> damaged = pristine;
        damaged[damaged.size() - 1] ^= 0xFF;
        writeSidecarBytes(path, damaged);
        CHECK(terrain::readTerrainLayerSidecar(path, generation, readMeta, loaded) ==
              terrain::TerrainLayerSidecarStatus::Invalid);
    }
}

TEST_CASE("VFTL refuses to apply a stack it cannot fully evaluate")
{
    ScopedTerrainTestFile file("sidecar-degraded");
    const auto path = sidecarTestPath(file);

    terrain::TerrainHeightLayerStore source;
    populateSidecarTestStore(source);

    const uint64_t generation = 0x2468ACE013579BDFull;
    REQUIRE(terrain::writeTerrainLayerSidecar(path, makeSidecarTestMeta(generation), source));

    // Rewrite the FIRST record's type to one this build has never heard of, then repair both
    // checksums so the file is otherwise indistinguishable from one a newer editor wrote.
    std::vector<uint8_t> bytes = readSidecarBytes(path);
    const size_t recordStart = static_cast<size_t>(terrain::TERRAIN_LAYER_HEADER_SIZE);

    uint32_t recordByteLength = 0;
    std::memcpy(&recordByteLength, bytes.data() + recordStart, sizeof(uint32_t));
    recordByteLength = resource::endian::fromLittleEndian(recordByteLength);

    const uint32_t alienType = resource::endian::toLittleEndian(uint32_t{9999});
    std::memcpy(bytes.data() + recordStart + sizeof(uint32_t) + sizeof(uint64_t), &alienType,
                sizeof(uint32_t));

    const size_t covered = recordByteLength - sizeof(uint32_t);
    const uint32_t recordCrc =
        resource::endian::toLittleEndian(resource::crc32(bytes.data() + recordStart, covered));
    std::memcpy(bytes.data() + recordStart + covered, &recordCrc, sizeof(uint32_t));
    resealSidecar(bytes);
    writeSidecarBytes(path, bytes);

    terrain::TerrainHeightLayerStore loaded;
    terrain::TerrainLayerSidecarMeta readMeta;
    CHECK(terrain::readTerrainLayerSidecar(path, generation, readMeta, loaded) ==
          terrain::TerrainLayerSidecarStatus::Degraded);

    // Nothing is applied, not even the records this build DOES understand. Half a stack composes
    // ground the artist never authored, and because coverage is sticky the tiles would stay
    // authoritative afterwards — silently wrong is worse than plainly unavailable.
    CHECK(loaded.empty());
    CHECK(loaded.layers().empty());
    CHECK(loaded.baseCount() == 0);
}

TEST_CASE("VFTL rebinding re-points a sidecar without decoding it")
{
    ScopedTerrainTestFile file("sidecar-rebind");
    const auto path = sidecarTestPath(file);

    terrain::TerrainHeightLayerStore source;
    populateSidecarTestStore(source);

    const uint64_t oldGeneration = 0x1111111111111111ull;
    const auto meta = makeSidecarTestMeta(oldGeneration);
    REQUIRE(terrain::writeTerrainLayerSidecar(path, meta, source));

    const uint64_t newGuid = 0x5555AAAA5555AAAAull;
    const uint64_t newGeneration = 0x9999999999999999ull;
    REQUIRE(terrain::rebindTerrainLayerSidecar(path, newGuid, newGeneration));

    terrain::TerrainHeightLayerStore loaded;
    terrain::TerrainLayerSidecarMeta readMeta;

    SUBCASE("the old generation no longer matches")
    {
        CHECK(terrain::readTerrainLayerSidecar(path, oldGeneration, readMeta, loaded) ==
              terrain::TerrainLayerSidecarStatus::Stale);
    }

    SUBCASE("the new one does, and the payload is intact")
    {
        REQUIRE(terrain::readTerrainLayerSidecar(path, newGeneration, readMeta, loaded) ==
                terrain::TerrainLayerSidecarStatus::Ok);
        CHECK(readMeta.terrainGuid == newGuid);
        CHECK(readMeta.generationId == newGeneration);
        CHECK(loaded.layers().size() == 2);
        CHECK(loaded.baseCount() == 3);
        for (const auto& entry : source.allBases())
        {
            const terrain::BaseHeightBlock* block = loaded.base(entry.first);
            REQUIRE(block != nullptr);
            CHECK(block->heights == entry.second.heights);
        }
    }

    SUBCASE("rebinding refuses a file it could not have written")
    {
        ScopedTerrainTestFile junkFile("sidecar-junk");
        const auto junkPath = sidecarTestPath(junkFile);
        writeSidecarBytes(junkPath, std::vector<uint8_t>(64, 0x7F));
        CHECK_FALSE(terrain::rebindTerrainLayerSidecar(junkPath, newGuid, newGeneration));
    }
}

TEST_CASE("VFTL writing refuses a stack it could not read back")
{
    ScopedTerrainTestFile file("sidecar-refusal");
    const auto path = sidecarTestPath(file);

    SUBCASE("a layer with no persistable type")
    {
        terrain::TerrainHeightLayerStore store;
        terrain::HeightLayerRecord record;
        record.id = 5;
        record.type = terrain::HeightLayerType::Unknown; // e.g. built in RAM by an older path
        store.addLayer(std::move(record));

        // Refusing beats writing a file that silently omits a layer the artist can see on screen.
        CHECK_FALSE(terrain::writeTerrainLayerSidecar(path, makeSidecarTestMeta(1), store));
    }

    SUBCASE("a base block whose array disagrees with its vertex count")
    {
        terrain::TerrainHeightLayerStore store;
        store.adoptBase({0, 0}, makeSidecarTestHeights(SIDECAR_TEST_VERTS, 0.0f),
                        SIDECAR_TEST_VERTS + 1);
        CHECK_FALSE(terrain::writeTerrainLayerSidecar(path, makeSidecarTestMeta(1), store));
    }
}

TEST_CASE("VFTL sidecar paths are appended, never substituted")
{
    // The same rule .vfmeta, .vfCollider and .vftrj follow. Substituting the extension would make
    // `Map.vfTerrain` and a hypothetical `Map.vfWorld` fight over one sidecar name.
    const terrain_test_fs::path terrainPath = "Assets/Maps/Skirmish.vfTerrain";
    CHECK(terrain::terrainLayerSidecarPath(terrainPath).string() ==
          terrainPath.string() + ".vfterrainlayers");
}

TEST_CASE("The layer store refuses to grow while editing is locked")
{
    terrain::TerrainHeightLayerStore store;
    store.setEditingLocked(true);
    CHECK(store.isEditingLocked());

    // A locked store is one whose authoritative bases could not be read. Accepting a layer would
    // let the next save write a sidecar describing ground nobody authored.
    CHECK_FALSE(store.addLayer(makeSidecarTestLayer(1, true, {{0, 0}})));
    CHECK(store.layers().empty());
    CHECK(store.empty());

    store.setEditingLocked(false);
    CHECK(store.addLayer(makeSidecarTestLayer(1, true, {{0, 0}})));
    CHECK(store.layers().size() == 1);
}

TEST_CASE("CRC-32 matches the published check value and detects single-bit damage")
{
    // The compile-time check in Crc32.hpp already pins the standard vector; this pins the
    // behaviour the format actually leans on.
    CHECK(resource::crc32(std::string_view{"123456789"}) == 0xCBF43926u);

    std::vector<uint8_t> bytes(256);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<uint8_t>(i);

    const uint32_t clean = resource::crc32(bytes.data(), bytes.size());
    for (size_t i : {size_t{0}, bytes.size() / 2, bytes.size() - 1})
    {
        std::vector<uint8_t> damaged = bytes;
        damaged[i] ^= 0x01;
        CHECK(resource::crc32(damaged.data(), damaged.size()) != clean);
    }

    // Streaming in pieces must equal hashing in one go, or a writer that emits a block in chunks
    // would disagree with a reader that checks it whole.
    uint32_t state = resource::crc32Init();
    state = resource::crc32Update(state, bytes.data(), 100);
    state = resource::crc32Update(state, bytes.data() + 100, bytes.size() - 100);
    CHECK(resource::crc32Finish(state) == clean);
}
