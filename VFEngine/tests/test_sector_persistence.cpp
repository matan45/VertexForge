#include <doctest.h>
#include <world/WorldSector.hpp>
#include <world/WorldSectorManager.hpp>
#include <world/SectorDataLayerOps.hpp>
#include <world/WorldSectorSerialization.hpp>
#include <world/WorldDefinition.hpp>
#include <world/WorldDefinitionSerialization.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <serialization/SerializationFileAccess.hpp>
#include <resource/EndianUtils.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
// .vfsector persistence: VFSC v2 binary round-trip, header-only
// metadata reads, JSON fallback, and dirty-flag lifecycle
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    fs::path testRoot()
    {
        return fs::temp_directory_path() / "vf_sector_persistence_tests";
    }

    void resetTestRoot()
    {
        std::error_code ec;
        fs::remove_all(testRoot(), ec);
        fs::create_directories(testRoot(), ec);
    }

    struct TestEntities
    {
        std::vector<scene::Entity> entities;

        // Live registry entities the sector serializer will pick up by UUID
        TestEntities(std::initializer_list<std::pair<uint64_t, glm::vec3>> specs)
        {
            scene::EntityRegistry::init(); // connect UUID lookup hooks (idempotent)
            for (const auto& [uuid, position] : specs)
            {
                scene::Entity entity("SectorEntity" + std::to_string(uuid));
                entity.addOrReplaceComponent<components::UUIDComponent>(uuid);
                entity.getComponent<components::TransformComponent>().position = position;
                entities.push_back(entity);
            }
        }

        ~TestEntities()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            for (auto& entity : entities)
            {
                if (registry.valid(entity.getHandle()))
                    registry.destroy(entity.getHandle());
            }
        }
    };

    world::WorldSector makeSector(const std::vector<uint64_t>& uuids)
    {
        world::WorldSector sector;
        sector.coord = {2, -3};
        for (uint64_t uuid : uuids)
            sector.entityUUIDs.push_back(uuid);
        sector.dirty = true;
        return sector;
    }

    std::vector<uint8_t> slurp(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        REQUIRE(file.is_open());
        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0);
        std::vector<uint8_t> bytes(size);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        return bytes;
    }

    template<typename T>
    void appendLE(std::vector<uint8_t>& bytes, T value)
    {
        const T le = resource::endian::toLittleEndian(value);
        const auto* raw = reinterpret_cast<const uint8_t*>(&le);
        bytes.insert(bytes.end(), raw, raw + sizeof(T));
    }

    // 44-byte header: magic | version | entityCount | 6x AABB float | totalFileSize
    void appendSectorHeader(std::vector<uint8_t>& bytes, uint32_t version, uint32_t entityCount,
                            uint64_t totalFileSize)
    {
        bytes.insert(bytes.end(), world::SECTOR_MAGIC.begin(), world::SECTOR_MAGIC.end());
        appendLE<uint32_t>(bytes, version);
        appendLE<uint32_t>(bytes, entityCount);
        for (int i = 0; i < 6; ++i)
            appendLE<float>(bytes, 0.0f);
        appendLE<uint64_t>(bytes, totalFileSize);
    }

    std::vector<uint8_t> entityMsgpack(uint64_t uuid, const char* name)
    {
        nlohmann::json sectorJson;
        sectorJson["version"] = "1.0";
        sectorJson["coord"] = {{"x", 0}, {"z", 0}};
        sectorJson["entities"] = nlohmann::json::array({{{"uuid", uuid}, {"name", name}}});
        return nlohmann::json::to_msgpack(sectorJson);
    }

    std::vector<uint8_t> makeV2SectorBytes(uint64_t uuid)
    {
        const auto blob = entityMsgpack(uuid, "Legacy");
        std::vector<uint8_t> bytes;
        appendSectorHeader(bytes, 2, 1, world::SECTOR_HEADER_SIZE + blob.size());
        bytes.insert(bytes.end(), blob.begin(), blob.end());
        return bytes;
    }

    // Serves .vfsector bytes from memory and reports archive mode, with no physical location —
    // exactly what a shipped LZ4-compressed .vfpak entry looks like to the reader.
    class ScopedSectorArchive
    {
    public:
        explicit ScopedSectorArchive(std::unordered_map<std::string, std::vector<uint8_t>> entries)
            : contents(std::make_shared<Entries>(std::move(entries)))
        {
            auto shared = contents;
            serialization::setSerializationFileAccess({
                [](const std::string&) -> std::optional<serialization::SerializationFileLocation>
                {
                    return std::nullopt;
                },
                [shared](const std::string& path)
                {
                    auto it = shared->find(path);
                    return it == shared->end() ? std::vector<uint8_t>{} : it->second;
                },
                [shared](const std::string& path) { return shared->contains(path); },
                [] { return true; }});
        }

        ScopedSectorArchive(const ScopedSectorArchive&) = delete;
        ScopedSectorArchive& operator=(const ScopedSectorArchive&) = delete;

        ~ScopedSectorArchive() { serialization::resetSerializationFileAccess(); }

    private:
        using Entries = std::unordered_map<std::string, std::vector<uint8_t>>;
        std::shared_ptr<Entries> contents;
    };
}

TEST_SUITE("SectorPersistence")
{
    TEST_CASE("binary save writes a header readable via readSectorMetadata")
    {
        resetTestRoot();
        TestEntities scope{{920001, {10.0f, 5.0f, 20.0f}}, {920002, {-30.0f, 2.0f, 40.0f}}};

        auto sector = makeSector({920001, 920002});
        std::string path = (testRoot() / "sector_2_-3.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSector(sector, path));

        world::SectorMetadata metadata;
        REQUIRE(world::WorldSectorSerialization::readSectorMetadata(path, metadata));
        CHECK(metadata.valid);
        CHECK(metadata.entityCount == 2);
        CHECK(metadata.bounds.min.x == doctest::Approx(-30.0f));
        CHECK(metadata.bounds.min.y == doctest::Approx(2.0f));
        CHECK(metadata.bounds.min.z == doctest::Approx(20.0f));
        CHECK(metadata.bounds.max.x == doctest::Approx(10.0f));
        CHECK(metadata.bounds.max.y == doctest::Approx(5.0f));
        CHECK(metadata.bounds.max.z == doctest::Approx(40.0f));
        CHECK(metadata.estimatedMemory == fs::file_size(path));
    }

    TEST_CASE("saveSector clears dirty and caches metadata on the sector")
    {
        resetTestRoot();
        TestEntities scope{{920010, {1.0f, 2.0f, 3.0f}}};

        auto sector = makeSector({920010});
        REQUIRE(sector.dirty);

        std::string path = (testRoot() / "dirty.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSector(sector, path));

        CHECK_FALSE(sector.dirty);
        CHECK(sector.filePath == path);
        CHECK(sector.metadata.valid);
        CHECK(sector.metadata.entityCount == 1);
    }

    TEST_CASE("loadSector returns the serialized entity payloads")
    {
        resetTestRoot();
        TestEntities scope{{920020, {0.0f, 0.0f, 0.0f}}, {920021, {7.0f, 0.0f, 0.0f}}};

        auto sector = makeSector({920020, 920021});
        std::string path = (testRoot() / "roundtrip.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSector(sector, path));

        std::vector<nlohmann::json> entityData;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, entityData));
        REQUIRE(entityData.size() == 2);

        std::vector<uint64_t> uuids;
        for (const auto& entityJson : entityData)
        {
            REQUIRE(entityJson.contains("uuid"));
            uuids.push_back(entityJson["uuid"].get<uint64_t>());
        }
        CHECK(std::find(uuids.begin(), uuids.end(), 920020) != uuids.end());
        CHECK(std::find(uuids.begin(), uuids.end(), 920021) != uuids.end());
    }

    TEST_CASE("entities missing from the registry are skipped at save time")
    {
        resetTestRoot();
        TestEntities scope{{920030, {0.0f, 0.0f, 0.0f}}};

        // 999999 is referenced by the sector but does not exist in the registry
        auto sector = makeSector({920030, 999999});
        std::string path = (testRoot() / "missing.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSector(sector, path));

        std::vector<nlohmann::json> entityData;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, entityData));
        CHECK(entityData.size() == 1);
    }

    TEST_CASE("loadSector auto-detects the JSON debug format")
    {
        resetTestRoot();
        TestEntities scope{{920040, {0.0f, 0.0f, 0.0f}}};

        auto sector = makeSector({920040});
        std::string path = (testRoot() / "debug.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSectorJson(sector, path));

        std::vector<nlohmann::json> entityData;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, entityData));
        CHECK(entityData.size() == 1);
    }

    TEST_CASE("readSectorMetadata rejects JSON sectors (binary header only)")
    {
        resetTestRoot();
        TestEntities scope{{920050, {0.0f, 0.0f, 0.0f}}};

        auto sector = makeSector({920050});
        std::string path = (testRoot() / "jsononly.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSectorJson(sector, path));

        world::SectorMetadata metadata;
        CHECK_FALSE(world::WorldSectorSerialization::readSectorMetadata(path, metadata));
        CHECK_FALSE(metadata.valid);
    }

    TEST_CASE("loadSector fails gracefully on bad input")
    {
        resetTestRoot();
        std::vector<nlohmann::json> entityData;

        SUBCASE("missing file")
        {
            CHECK_FALSE(world::WorldSectorSerialization::loadSector(
                (testRoot() / "nope.vfsector").string(), entityData));
        }

        SUBCASE("garbage content")
        {
            std::string path = (testRoot() / "garbage.vfsector").string();
            std::ofstream file(path, std::ios::binary);
            file << "this is not a sector file";
            file.close();
            CHECK_FALSE(world::WorldSectorSerialization::loadSector(path, entityData));
        }

        SUBCASE("unsupported format version")
        {
            TestEntities scope{{920060, {0.0f, 0.0f, 0.0f}}};
            auto sector = makeSector({920060});
            std::string path = (testRoot() / "future.vfsector").string();
            REQUIRE(world::WorldSectorSerialization::saveSector(sector, path));

            // Patch the version field (bytes 4..7, little-endian) to 99
            std::ifstream in(path, std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
            in.close();
            REQUIRE(bytes.size() > 8);
            bytes[4] = 99; bytes[5] = 0; bytes[6] = 0; bytes[7] = 0;
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            out.close();

            CHECK_FALSE(world::WorldSectorSerialization::loadSector(path, entityData));
        }
    }

    TEST_CASE("v3 sections round-trip data layers alongside entities")
    {
        resetTestRoot();
        TestEntities scope{{920070, {1.0f, 0.0f, 1.0f}}};

        auto sector = makeSector({920070});
        sector.dataLayers["fogOfWar"] = {0x01, 0x02, 0x03, 0xFF};
        sector.dataLayers["resourceGrid"] = std::vector<uint8_t>(256, 0xAB);

        std::string path = (testRoot() / "layers.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSector(sector, path));

        std::vector<nlohmann::json> entityData;
        world::SectorDataLayers layers;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, entityData, &layers));

        CHECK(entityData.size() == 1);
        REQUIRE(layers.size() == 2);
        CHECK(layers.at("fogOfWar") == std::vector<uint8_t>{0x01, 0x02, 0x03, 0xFF});
        CHECK(layers.at("resourceGrid") == std::vector<uint8_t>(256, 0xAB));

        // Callers that don't ask for layers still load entities
        std::vector<nlohmann::json> entitiesOnly;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, entitiesOnly));
        CHECK(entitiesOnly.size() == 1);
    }

    TEST_CASE("v2 sector files still load (backward compatibility)")
    {
        resetTestRoot();

        // Hand-craft a v2 file: 44-byte header + raw MessagePack payload
        nlohmann::json sectorJson;
        sectorJson["version"] = "1.0";
        sectorJson["coord"] = {{"x", 0}, {"z", 0}};
        sectorJson["entities"] = nlohmann::json::array(
            {{{"uuid", 920080}, {"name", "Legacy"}}});
        auto blob = nlohmann::json::to_msgpack(sectorJson);

        std::string path = (testRoot() / "legacy_v2.vfsector").string();
        {
            std::ofstream file(path, std::ios::binary);
            REQUIRE(file.is_open());
            file.write("VFSC", 4);
            auto writeU32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
            auto writeF32 = [&](float v) { file.write(reinterpret_cast<const char*>(&v), 4); };
            writeU32(2); // version
            writeU32(1); // entity count
            for (int i = 0; i < 6; ++i) writeF32(0.0f); // AABB
            uint64_t totalSize = 44 + blob.size();
            file.write(reinterpret_cast<const char*>(&totalSize), 8);
            file.write(reinterpret_cast<const char*>(blob.data()),
                       static_cast<std::streamsize>(blob.size()));
        }

        std::vector<nlohmann::json> entityData;
        world::SectorDataLayers layers;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, entityData, &layers));
        REQUIRE(entityData.size() == 1);
        CHECK(entityData[0]["uuid"].get<uint64_t>() == 920080);
        CHECK(layers.empty()); // v2 has no sections

        world::SectorMetadata metadata;
        REQUIRE(world::WorldSectorSerialization::readSectorMetadata(path, metadata));
        CHECK(metadata.entityCount == 1);
    }

    TEST_CASE(".vfworld round-trips the full streaming config")
    {
        resetTestRoot();

        world::WorldDefinition definition;
        definition.name = "RoundTrip";
        definition.primaryGrid().sectorConfig.sectorWorldSize = 256.0f;
        definition.primaryGrid().sectorConfig.tilesPerSector = 8;
        definition.primaryGrid().streamingConfig.loadRadius = 6.0f;
        definition.primaryGrid().streamingConfig.unloadRadius = 9.0f;
        definition.primaryGrid().streamingConfig.maxLoadsPerFrame = 3;
        definition.primaryGrid().streamingConfig.maxEntitiesPerFrame = 16;
        definition.primaryGrid().streamingConfig.editModeStreaming = true;
        definition.primaryGrid().sectorFilePaths[{1, -2}] = "sectors/sector_1_-2.vfsector";

        std::string path = (testRoot() / "roundtrip.vfworld").string();
        REQUIRE(world::WorldDefinitionSerialization::save(definition, path));

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));
        CHECK(loaded.name == "RoundTrip");
        CHECK(loaded.primaryGrid().sectorConfig.sectorWorldSize == doctest::Approx(256.0f));
        CHECK(loaded.primaryGrid().sectorConfig.tilesPerSector == 8);
        CHECK(loaded.primaryGrid().streamingConfig.loadRadius == doctest::Approx(6.0f));
        CHECK(loaded.primaryGrid().streamingConfig.unloadRadius == doctest::Approx(9.0f));
        CHECK(loaded.primaryGrid().streamingConfig.maxLoadsPerFrame == 3);
        CHECK(loaded.primaryGrid().streamingConfig.maxEntitiesPerFrame == 16);
        CHECK(loaded.primaryGrid().streamingConfig.editModeStreaming == true);
        REQUIRE(loaded.primaryGrid().sectorFilePaths.size() == 1);
        CHECK(loaded.primaryGrid().sectorFilePaths.at({1, -2}) == "sectors/sector_1_-2.vfsector");
    }

    // ── VK-1600 review: the .vfworld read path is untrusted input ───────

    TEST_CASE(".vfworld round-trips the prefetchRadius sentinel unresolved")
    {
        // 0 means "no prefetch ring". It has to survive save/load VERBATIM: resolving it to the
        // current loadRadius bakes today's value in, so a later loadRadius edit silently stops
        // widening the prefetch ring with it. The two call sites that used to normalize the
        // PERSISTED config (SetStreamingConfigCommand, addGrid) sanitize instead for this reason.
        resetTestRoot();

        world::WorldDefinition definition;
        definition.name = "Sentinel";
        definition.primaryGrid().streamingConfig.loadRadius = 8.0f;
        definition.primaryGrid().streamingConfig.prefetchRadius = 0.0f;

        const std::string path = (testRoot() / "sentinel.vfworld").string();
        REQUIRE(world::WorldDefinitionSerialization::save(definition, path));

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));

        CHECK(loaded.primaryGrid().streamingConfig.prefetchRadius == doctest::Approx(0.0f));
        CHECK(loaded.primaryGrid().streamingConfig.loadRadius == doctest::Approx(8.0f));

        // ...and the sentinel still reads as "same as loadRadius" rather than as a literal 0 ring.
        CHECK(world::effectivePrefetchRadius(loaded.primaryGrid().streamingConfig)
              == doctest::Approx(8.0f));
    }

    TEST_CASE(".vfworld loading clamps a hostile streaming config")
    {
        // A hand-edited, plugin-written or older-tool .vfworld can carry anything; readStreamingConfig
        // is 30 bare in.value() calls. The clamp has to happen where the bytes become a config,
        // because a negative per-frame budget reaches std::vector::reserve as a near-SIZE_MAX count
        // and a negative radius stalls streaming outright.
        resetTestRoot();
        const std::string path = (testRoot() / "hostile.vfworld").string();

        {
            world::WorldDefinition definition;
            definition.name = "Hostile";
            REQUIRE(world::WorldDefinitionSerialization::save(definition, path));
        }

        // Rewrite the saved file with values no UI can produce.
        nlohmann::json doc;
        {
            std::ifstream in(path);
            REQUIRE(in.is_open());
            in >> doc;
        }
        doc["streamingConfig"]["maxLoadsPerFrame"] = -1;
        doc["streamingConfig"]["maxUnloadsPerFrame"] = -100;
        doc["streamingConfig"]["loadRadius"] = -4.0f;
        doc["sectorConfig"]["sectorWorldSize"] = 0.0f;
        {
            std::ofstream out(path);
            REQUIRE(out.is_open());
            out << doc.dump(2);
        }

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));

        const auto& streaming = loaded.primaryGrid().streamingConfig;
        CHECK(streaming.maxLoadsPerFrame >= 0);
        CHECK(streaming.maxUnloadsPerFrame >= 0);
        CHECK(streaming.loadRadius > 0.0f);
        CHECK(loaded.primaryGrid().sectorConfig.sectorWorldSize > 0.0f);
    }

    // ── VK-1594: per-cell HLOD bake inventory ───────────────────────────

    TEST_CASE(".vfworld round-trips the hlodCells inventory across all tiers")
    {
        resetTestRoot();

        world::WorldDefinition definition;
        definition.name = "HlodCells";
        definition.hlodCells[world::HLODCellCoord(4, 2, 0)] = "sectors/sector_4_2_hlod0.vfHLOD";
        definition.hlodCells[world::HLODCellCoord(2, 1, 1)] = "worlds/w_hlod1_2_1.vfHLOD";
        definition.hlodCells[world::HLODCellCoord(-3, -2, 2)] = "worlds/w_hlod2_-3_-2.vfHLOD";

        std::string path = (testRoot() / "hlodcells.vfworld").string();
        REQUIRE(world::WorldDefinitionSerialization::save(definition, path));

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));

        REQUIRE(loaded.hlodCells.size() == 3);
        CHECK(loaded.hlodCells.at(world::HLODCellCoord(4, 2, 0)) == "sectors/sector_4_2_hlod0.vfHLOD");
        CHECK(loaded.hlodCells.at(world::HLODCellCoord(2, 1, 1)) == "worlds/w_hlod1_2_1.vfHLOD");
        // Negative cell coords survive the JSON round-trip
        CHECK(loaded.hlodCells.at(world::HLODCellCoord(-3, -2, 2)) == "worlds/w_hlod2_-3_-2.vfHLOD");

        // The tier is part of the key: same x/z at a different tier is a different cell
        CHECK(loaded.hlodCells.find(world::HLODCellCoord(2, 1, 0)) == loaded.hlodCells.end());
    }

    TEST_CASE(".vfworld written with no bakes omits the hlodCells key entirely")
    {
        resetTestRoot();

        // Byte-compatibility with pre-VK-1594 output: a world that was never baked must not
        // suddenly grow a key, or every existing .vfworld shows a diff on its next save.
        world::WorldDefinition definition;
        definition.name = "NoBakes";

        std::string path = (testRoot() / "nobakes.vfworld").string();
        REQUIRE(world::WorldDefinitionSerialization::save(definition, path));

        std::ifstream in{path};
        REQUIRE(in.is_open());
        nlohmann::json parsed = nlohmann::json::parse(in);
        CHECK_FALSE(parsed.contains("hlodCells"));
    }

    TEST_CASE(".vfworld without hlodCells loads an empty inventory, not a failure")
    {
        resetTestRoot();

        // Every world baked before VK-1594 looks like this. The tier-0 fallback on
        // WorldSector::hlodFilePath is what keeps those bakes resolving.
        nlohmann::json legacy;
        legacy["version"] = "1.0";
        legacy["name"] = "LegacyBakes";
        legacy["sectors"] = nlohmann::json::array();

        std::string path = (testRoot() / "legacy_hlod.vfworld").string();
        {
            std::ofstream out{path};
            REQUIRE(out.is_open());
            out << legacy.dump(2);
        }

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));
        CHECK(loaded.hlodCells.empty());
    }

    TEST_CASE(".vfworld skips hlodCells entries with an empty path")
    {
        resetTestRoot();

        // An invalidated cell clears its path rather than erasing the key in some paths; a blank
        // entry must never come back as a bake the streamer would try to read.
        nlohmann::json doc;
        doc["version"] = "1.0";
        doc["name"] = "BlankPath";
        doc["sectors"] = nlohmann::json::array();
        doc["hlodCells"] = nlohmann::json::array({
            nlohmann::json{{"x", 0}, {"z", 0}, {"tier", 1}, {"path", ""}},
            nlohmann::json{{"x", 1}, {"z", 0}, {"tier", 1}, {"path", "worlds/w_hlod1_1_0.vfHLOD"}}});

        std::string path = (testRoot() / "blankpath.vfworld").string();
        {
            std::ofstream out{path};
            REQUIRE(out.is_open());
            out << doc.dump(2);
        }

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));
        REQUIRE(loaded.hlodCells.size() == 1);
        CHECK(loaded.hlodCells.count(world::HLODCellCoord(1, 0, 1)) == 1);
    }

    // ── VK-1588: missing keys must resolve to the struct defaults ───────

    TEST_CASE(".vfworld with the streaming keys absent parses to exact struct defaults")
    {
        resetTestRoot();

        // A minimal hand-written .vfworld: no sectorConfig, no streamingConfig, no hlodConfig -
        // i.e. a file written before those blocks existed. The loader used to fall back to
        // hardcoded literals, and loadRadius/unloadRadius were 512/640 WORLD UNITS copy-pasted
        // from the terrain streamer while the struct means SECTOR COUNTS, so this file produced a
        // 512-sector load ring instead of a 4-sector one.
        nlohmann::json minimal;
        minimal["version"] = "1.0";
        minimal["name"] = "LegacyWorld";
        minimal["sectors"] = nlohmann::json::array();

        std::string path = (testRoot() / "legacy.vfworld").string();
        {
            std::ofstream out{path};
            REQUIRE(out.is_open());
            out << minimal.dump(2);
        }

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));

        const world::SectorConfig sectorDefaults;
        const world::SectorStreamingConfig d;

        CHECK(loaded.name == "LegacyWorld");

        SUBCASE("sectorConfig falls back to the struct")
        {
            CHECK(loaded.primaryGrid().sectorConfig.sectorWorldSize == doctest::Approx(sectorDefaults.sectorWorldSize));
            CHECK(loaded.primaryGrid().sectorConfig.tilesPerSector == sectorDefaults.tilesPerSector);
            CHECK(loaded.primaryGrid().sectorConfig.alignedToTerrain == sectorDefaults.alignedToTerrain);
        }
        SUBCASE("every streamingConfig field falls back to the struct")
        {
            CHECK(loaded.primaryGrid().streamingConfig.loadRadius == doctest::Approx(d.loadRadius));
            CHECK(loaded.primaryGrid().streamingConfig.unloadRadius == doctest::Approx(d.unloadRadius));
            CHECK(loaded.primaryGrid().streamingConfig.maxLoadsPerFrame == d.maxLoadsPerFrame);
            CHECK(loaded.primaryGrid().streamingConfig.maxUnloadsPerFrame == d.maxUnloadsPerFrame);
            CHECK(loaded.primaryGrid().streamingConfig.maxEntitiesPerFrame == d.maxEntitiesPerFrame);
            CHECK(loaded.primaryGrid().streamingConfig.maxTerrainLoadsPerFrame == d.maxTerrainLoadsPerFrame);
            CHECK(loaded.primaryGrid().streamingConfig.maxTerrainUnloadsPerFrame == d.maxTerrainUnloadsPerFrame);
            CHECK(loaded.primaryGrid().streamingConfig.enableGPUObjectStreaming == d.enableGPUObjectStreaming);
            CHECK(loaded.primaryGrid().streamingConfig.editModeStreaming == d.editModeStreaming);
            CHECK(loaded.primaryGrid().streamingConfig.hlodTier0Radius == doctest::Approx(d.hlodTier0Radius));
            CHECK(loaded.primaryGrid().streamingConfig.hlodTier1Radius == doctest::Approx(d.hlodTier1Radius));
            CHECK(loaded.primaryGrid().streamingConfig.hlodTier2Radius == doctest::Approx(d.hlodTier2Radius));
        }
        SUBCASE("an absent hlodConfig still yields the three default tiers")
        {
            // A default-constructed HLODConfig has an EMPTY tiers vector - only
            // HLODConfig::defaultConfig() supplies the hierarchy, so the loader must keep using it.
            REQUIRE(loaded.hlodConfig.tiers.size() == 3);
            CHECK(loaded.hlodConfig.tiers[0].tier == 0);
            CHECK(loaded.hlodConfig.tiers[1].tier == 1);
            CHECK(loaded.hlodConfig.tiers[2].tier == 2);
        }
    }

    TEST_CASE(".vfworld with a partial streamingConfig keeps struct defaults for the absent keys")
    {
        resetTestRoot();

        // The exact shape of the bug: the block exists, so the loader entered it, but the radius
        // keys were missing and picked up the world-unit literals.
        nlohmann::json partial;
        partial["name"] = "PartialWorld";
        partial["streamingConfig"]["loadRadius"] = 7.0f;

        std::string path = (testRoot() / "partial.vfworld").string();
        {
            std::ofstream out{path};
            REQUIRE(out.is_open());
            out << partial.dump(2);
        }

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));

        const world::SectorStreamingConfig d;
        CHECK(loaded.primaryGrid().streamingConfig.loadRadius == doctest::Approx(7.0f));
        CHECK(loaded.primaryGrid().streamingConfig.unloadRadius == doctest::Approx(d.unloadRadius));
        CHECK(loaded.primaryGrid().streamingConfig.maxEntitiesPerFrame == d.maxEntitiesPerFrame);
        CHECK(loaded.primaryGrid().streamingConfig.enableGPUObjectStreaming == d.enableGPUObjectStreaming);
    }

    // ── VK-1587: buffer parsing + archive-mode routing ──────────────────

    TEST_CASE("loadSectorFromMemory matches the on-disk v3 load")
    {
        resetTestRoot();
        TestEntities scope{{920090, {1.0f, 2.0f, 3.0f}}, {920091, {-4.0f, 0.0f, 5.0f}}};

        auto sector = makeSector({920090, 920091});
        sector.dataLayers["fogOfWar"] = {0x01, 0x02, 0x03, 0xFF};
        sector.dataLayers["resourceGrid"] = std::vector<uint8_t>(256, 0xAB);

        std::string path = (testRoot() / "frommemory_v3.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSector(sector, path));

        std::vector<nlohmann::json> fromFile;
        world::SectorDataLayers fileLayers;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, fromFile, &fileLayers));

        std::vector<nlohmann::json> fromMemory;
        world::SectorDataLayers memoryLayers;
        REQUIRE(world::WorldSectorSerialization::loadSectorFromMemory(
            slurp(path), fromMemory, &memoryLayers));

        CHECK(fromMemory == fromFile);
        REQUIRE(memoryLayers.size() == 2);
        CHECK(memoryLayers.at("fogOfWar") == fileLayers.at("fogOfWar"));
        CHECK(memoryLayers.at("resourceGrid") == fileLayers.at("resourceGrid"));

        // Callers that don't ask for layers still get the entities
        std::vector<nlohmann::json> entitiesOnly;
        REQUIRE(world::WorldSectorSerialization::loadSectorFromMemory(slurp(path), entitiesOnly));
        CHECK(entitiesOnly.size() == 2);
    }

    TEST_CASE("loadSectorFromMemory reads a v2 buffer that never touched disk")
    {
        const auto bytes = makeV2SectorBytes(920100);

        std::vector<nlohmann::json> entityData;
        world::SectorDataLayers layers;
        REQUIRE(world::WorldSectorSerialization::loadSectorFromMemory(bytes, entityData, &layers));
        REQUIRE(entityData.size() == 1);
        CHECK(entityData[0]["uuid"].get<uint64_t>() == 920100);
        CHECK(layers.empty()); // v2 has no sections
    }

    TEST_CASE("loadSectorFromMemory rejects truncated and corrupted buffers")
    {
        std::vector<nlohmann::json> entityData;

        SUBCASE("empty buffer")
        {
            CHECK_FALSE(world::WorldSectorSerialization::loadSectorFromMemory({}, entityData));
        }

        SUBCASE("shorter than the header")
        {
            std::vector<uint8_t> bytes{'V', 'F', 'S', 'C', 3, 0, 0, 0};
            CHECK_FALSE(world::WorldSectorSerialization::loadSectorFromMemory(bytes, entityData));
        }

        SUBCASE("header only, no payload")
        {
            std::vector<uint8_t> bytes;
            appendSectorHeader(bytes, 3, 1, world::SECTOR_HEADER_SIZE);
            CHECK_FALSE(world::WorldSectorSerialization::loadSectorFromMemory(bytes, entityData));
        }

        SUBCASE("entity blob shorter than its declared size")
        {
            const auto blob = entityMsgpack(920110, "Truncated");
            std::vector<uint8_t> bytes;
            appendSectorHeader(bytes, 3, 1, world::SECTOR_HEADER_SIZE + 8 + blob.size());
            appendLE<uint64_t>(bytes, blob.size());
            bytes.insert(bytes.end(), blob.begin(), blob.end() - 1); // one byte short
            CHECK_FALSE(world::WorldSectorSerialization::loadSectorFromMemory(bytes, entityData));
        }

        SUBCASE("section declares more bytes than the buffer holds")
        {
            const auto blob = entityMsgpack(920111, "BadSection");
            std::vector<uint8_t> bytes;
            appendSectorHeader(bytes, 3, 1, world::SECTOR_HEADER_SIZE + 8 + blob.size() + 16);
            appendLE<uint64_t>(bytes, blob.size());
            bytes.insert(bytes.end(), blob.begin(), blob.end());
            appendLE<uint32_t>(bytes, 1);                              // sectionCount
            appendLE<uint32_t>(bytes, world::SECTOR_SECTION_DATA_LAYERS);
            appendLE<uint64_t>(bytes, 1ULL << 40);                     // 1 TB of "payload"

            world::SectorDataLayers layers;
            CHECK_FALSE(world::WorldSectorSerialization::loadSectorFromMemory(
                bytes, entityData, &layers));
        }

        SUBCASE("totalFileSize smaller than the header")
        {
            auto bytes = makeV2SectorBytes(920112);
            const uint64_t bogus = resource::endian::toLittleEndian<uint64_t>(10);
            std::memcpy(bytes.data() + 36, &bogus, sizeof(bogus));
            CHECK_FALSE(world::WorldSectorSerialization::loadSectorFromMemory(bytes, entityData));
        }

        SUBCASE("totalFileSize beyond the 256 MB cap")
        {
            auto bytes = makeV2SectorBytes(920113);
            const uint64_t bogus = resource::endian::toLittleEndian<uint64_t>(300ULL * 1024 * 1024);
            std::memcpy(bytes.data() + 36, &bogus, sizeof(bogus));
            CHECK_FALSE(world::WorldSectorSerialization::loadSectorFromMemory(bytes, entityData));
        }
    }

    TEST_CASE("readSectorMetadataFromMemory matches the header read from disk")
    {
        resetTestRoot();
        TestEntities scope{{920120, {10.0f, 5.0f, 20.0f}}, {920121, {-30.0f, 2.0f, 40.0f}}};

        auto sector = makeSector({920120, 920121});
        std::string path = (testRoot() / "metaparity.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSector(sector, path));

        world::SectorMetadata fromFile;
        REQUIRE(world::WorldSectorSerialization::readSectorMetadata(path, fromFile));

        world::SectorMetadata fromMemory;
        REQUIRE(world::WorldSectorSerialization::readSectorMetadataFromMemory(slurp(path), fromMemory));

        CHECK(fromMemory.valid == fromFile.valid);
        CHECK(fromMemory.entityCount == fromFile.entityCount);
        CHECK(fromMemory.estimatedMemory == fromFile.estimatedMemory);
        CHECK(fromMemory.bounds.min.x == doctest::Approx(fromFile.bounds.min.x));
        CHECK(fromMemory.bounds.min.y == doctest::Approx(fromFile.bounds.min.y));
        CHECK(fromMemory.bounds.min.z == doctest::Approx(fromFile.bounds.min.z));
        CHECK(fromMemory.bounds.max.x == doctest::Approx(fromFile.bounds.max.x));
        CHECK(fromMemory.bounds.max.y == doctest::Approx(fromFile.bounds.max.y));
        CHECK(fromMemory.bounds.max.z == doctest::Approx(fromFile.bounds.max.z));

        SUBCASE("a JSON sector still has no binary header to read")
        {
            std::string jsonPath = (testRoot() / "metaparity.json.vfsector").string();
            REQUIRE(world::WorldSectorSerialization::saveSectorJson(sector, jsonPath));

            world::SectorMetadata metadata;
            CHECK_FALSE(world::WorldSectorSerialization::readSectorMetadataFromMemory(
                slurp(jsonPath), metadata));
            CHECK_FALSE(metadata.valid);
        }
    }

    TEST_CASE("archive mode streams sectors that exist only inside the pak")
    {
        // The exporter keys entries "Assets/<relative>"; nothing of the sort exists on disk here,
        // so a pass proves the read went through the file-access bridge and not the filesystem.
        const std::string archiveKey = "Assets/Worlds/sectors/sector_2_-3.vfsector";
        REQUIRE_FALSE(fs::exists(archiveKey));

        ScopedSectorArchive archive{{{archiveKey, makeV2SectorBytes(920130)}}};

        std::vector<nlohmann::json> entityData;
        REQUIRE(world::WorldSectorSerialization::loadSector(archiveKey, entityData));
        REQUIRE(entityData.size() == 1);
        CHECK(entityData[0]["uuid"].get<uint64_t>() == 920130);

        // locate() reports no physical bytes for a compressed entry, so this falls back to
        // inflating the whole entry rather than seeking 44 bytes
        world::SectorMetadata metadata;
        REQUIRE(world::WorldSectorSerialization::readSectorMetadata(archiveKey, metadata));
        CHECK(metadata.valid);
        CHECK(metadata.entityCount == 1);

        std::vector<nlohmann::json> missing;
        CHECK_FALSE(world::WorldSectorSerialization::loadSector(
            "Assets/Worlds/sectors/sector_9_9.vfsector", missing));
    }

    TEST_CASE("dirty lifecycle: manager mutations mark sectors dirty")
    {
        world::SectorConfig config;
        config.sectorWorldSize = 100.0f;
        world::WorldSectorManager manager(config);

        manager.assignEntityToSector(930001, glm::vec3(50.0f, 0.0f, 50.0f));
        auto* sector = manager.getSector({0, 0});
        REQUIRE(sector != nullptr);
        CHECK(sector->dirty);
        CHECK(manager.hasEntitySector(930001));
        CHECK(manager.getEntitySector(930001) == world::SectorCoord(0, 0));

        sector->dirty = false; // simulate a save

        manager.removeEntityFromSector(930001, {0, 0});
        CHECK(sector->dirty); // removal re-dirties
        CHECK_FALSE(manager.hasEntitySector(930001));
    }

    TEST_CASE("dirty lifecycle: cross-sector reassign dirties both sectors")
    {
        world::SectorConfig config;
        config.sectorWorldSize = 100.0f;
        world::WorldSectorManager manager(config);

        manager.assignEntityToSector(930002, glm::vec3(50.0f, 0.0f, 50.0f));
        manager.getSector({0, 0})->dirty = false;

        manager.reassignEntity(930002, glm::vec3(50.0f, 0.0f, 50.0f),
                               glm::vec3(150.0f, 0.0f, 50.0f));
        CHECK(manager.getSector({0, 0})->dirty);
        REQUIRE(manager.getSector({1, 0}) != nullptr);
        CHECK(manager.getSector({1, 0})->dirty);
        CHECK(manager.getEntitySector(930002) == world::SectorCoord(1, 0));
    }

    TEST_CASE("dirty lifecycle: intra-sector reassign is a no-op (known gap)")
    {
        // Documents the current data-loss gap: moving an entity within its sector
        // leaves the sector clean, so Save World skips it. Phase 1 (B1) changes this
        // at the service layer; the manager-level early-out stays.
        world::SectorConfig config;
        config.sectorWorldSize = 100.0f;
        world::WorldSectorManager manager(config);

        manager.assignEntityToSector(930003, glm::vec3(10.0f, 0.0f, 10.0f));
        manager.getSector({0, 0})->dirty = false;

        manager.reassignEntity(930003, glm::vec3(10.0f, 0.0f, 10.0f),
                               glm::vec3(90.0f, 0.0f, 90.0f));
        CHECK_FALSE(manager.getSector({0, 0})->dirty);
    }

    // ── VK-1596: data layer authoring ────────────────────────────────────────────────
    //
    // WorldSectorServiceImpl is not unit-constructible, so these exercise the pure helpers the
    // service delegates to, plus the file round-trip the editor's create/toggle drives.

    TEST_CASE("canMutateDataLayers refuses every sector whose entityUUIDs are not authoritative")
    {
        world::WorldSector sector;

        sector.state = world::SectorState::Loaded;
        CHECK(world::canMutateDataLayers(sector));

        // The whole point of the guard: an Unloaded sector's entityUUIDs has been stripped, so
        // dirtying it makes Save World rewrite its .vfsector with an empty entity list.
        sector.state = world::SectorState::Unloaded;
        CHECK_FALSE(world::canMutateDataLayers(sector));

        // Loading is refused for a second reason - finalizeSectorLoad would clear the dirty flag
        // the write had just set.
        sector.state = world::SectorState::Loading;
        CHECK_FALSE(world::canMutateDataLayers(sector));

        sector.state = world::SectorState::Unloading;
        CHECK_FALSE(world::canMutateDataLayers(sector));
        sector.state = world::SectorState::Prefetching;
        CHECK_FALSE(world::canMutateDataLayers(sector));
        sector.state = world::SectorState::Prefetched;
        CHECK_FALSE(world::canMutateDataLayers(sector));
    }

    TEST_CASE("summarizeDataLayers unions layers across loaded sectors only")
    {
        world::SectorConfig config;
        config.sectorWorldSize = 100.0f;
        world::WorldSectorManager manager(config);

        auto& a = manager.getOrCreateSector({0, 0});
        a.state = world::SectorState::Loaded;
        a.dataLayers["fogOfWar"] = {0x01, 0x02, 0x03, 0x04};
        a.dataLayers["resourceGrid"] = std::vector<uint8_t>(256, 0xAB);

        auto& b = manager.getOrCreateSector({1, 0});
        b.state = world::SectorState::Loaded;
        b.dataLayers["fogOfWar"] = {0x05, 0x06};

        // Carries a layer but must not be summarized: its map is stale until it loads.
        auto& c = manager.getOrCreateSector({2, 0});
        c.state = world::SectorState::Unloaded;
        c.dataLayers["fogOfWar"] = std::vector<uint8_t>(1024, 0xFF);

        // Bytes resident, no live layers.
        auto& d = manager.getOrCreateSector({3, 0});
        d.state = world::SectorState::Prefetched;

        world::DataLayerInventory inventory;
        world::summarizeDataLayers(manager, inventory);

        REQUIRE(inventory.loadedSectors.size() == 2);
        CHECK(inventory.loadedSectors[0] == world::SectorCoord(0, 0));
        CHECK(inventory.loadedSectors[1] == world::SectorCoord(1, 0));

        REQUIRE(inventory.layers.size() == 2);
        // Sorted by name: the backing store is an unordered_map, so without the sort the editor's
        // table would reshuffle on every 0.25s poll.
        CHECK(inventory.layers[0].name == "fogOfWar");
        CHECK(inventory.layers[1].name == "resourceGrid");

        CHECK(inventory.layers[0].sectorCount == 2);
        CHECK(inventory.layers[0].totalBytes == 6); // 4 + 2, the unloaded sector's 1024 excluded
        REQUIRE(inventory.layers[0].sectors.size() == 2);
        CHECK(inventory.layers[0].sectors[0] == world::SectorCoord(0, 0));
        CHECK(inventory.layers[0].sectors[1] == world::SectorCoord(1, 0));

        CHECK(inventory.layers[1].sectorCount == 1);
        CHECK(inventory.layers[1].totalBytes == 256);

        SUBCASE("a world with no loaded sectors summarizes to nothing")
        {
            a.state = world::SectorState::Unloaded;
            b.state = world::SectorState::Unloaded;

            world::summarizeDataLayers(manager, inventory);
            CHECK(inventory.loadedSectors.empty());
            CHECK(inventory.layers.empty());
        }
    }

    TEST_CASE("mergeSectorDataLayers keeps resident layers and reports unsaved work")
    {
        SUBCASE("nothing resident: the file wins outright and nothing is unsaved")
        {
            world::SectorDataLayers resident;
            world::SectorDataLayers incoming{{"fogOfWar", {0x01, 0x02}}};

            CHECK_FALSE(world::mergeSectorDataLayers(resident, incoming));
            REQUIRE(resident.size() == 1);
            CHECK(resident.at("fogOfWar") == std::vector<uint8_t>{0x01, 0x02});
        }

        SUBCASE("a resident layer absent from the file is unsaved work")
        {
            world::SectorDataLayers resident{{"spawnMask", {0x09}}};
            world::SectorDataLayers incoming{{"fogOfWar", {0x01}}};

            CHECK(world::mergeSectorDataLayers(resident, incoming));
            CHECK(resident.size() == 2);
            CHECK(resident.at("spawnMask") == std::vector<uint8_t>{0x09});
        }

        SUBCASE("a resident layer that differs from the file is unsaved work")
        {
            world::SectorDataLayers resident{{"fogOfWar", {0xAA}}};
            world::SectorDataLayers incoming{{"fogOfWar", {0x01}}};

            CHECK(world::mergeSectorDataLayers(resident, incoming));
            // Resident wins: a runtime write is newer than the file.
            CHECK(resident.at("fogOfWar") == std::vector<uint8_t>{0xAA});
        }

        SUBCASE("a resident layer identical to the file is not unsaved work")
        {
            world::SectorDataLayers resident{{"fogOfWar", {0x01, 0x02}}};
            world::SectorDataLayers incoming{{"fogOfWar", {0x01, 0x02}},
                                             {"resourceGrid", {0x07}}};

            CHECK_FALSE(world::mergeSectorDataLayers(resident, incoming));
            CHECK(resident.size() == 2);
        }
    }

    TEST_CASE("VK-1596: create, save and reload a data layer at the core level")
    {
        resetTestRoot();
        TestEntities scope{{920090, {10.0f, 0.0f, 10.0f}}};

        world::SectorConfig config;
        config.sectorWorldSize = 100.0f;
        world::WorldSectorManager manager(config);
        manager.assignEntityToSector(920090, glm::vec3(10.0f, 0.0f, 10.0f));

        auto* sector = manager.getSector({0, 0});
        REQUIRE(sector != nullptr);
        sector->state = world::SectorState::Loaded;
        sector->dirty = false;

        // create - the editor's Create button, via SetSectorDataLayerCommand
        REQUIRE(world::canMutateDataLayers(*sector));
        sector->dataLayers["fogOfWar"] = {0x01, 0x02, 0x03};
        sector->dirty = true; // the service's edit-mode rule

        const std::string path = (testRoot() / "layer_roundtrip.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSector(*sector, path));
        CHECK_FALSE(sector->dirty); // cleared by the save

        // unload: entityUUIDs is stripped, exactly as handleSectorUnload leaves it
        sector->entityUUIDs.clear();
        sector->state = world::SectorState::Unloaded;
        // ... and the sector is now un-editable, which is what stops Save World truncating it
        CHECK_FALSE(world::canMutateDataLayers(*sector));

        // reload: the file merges UNDER whatever survived on the struct
        std::vector<nlohmann::json> entityData;
        world::SectorDataLayers fileLayers;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, entityData, &fileLayers));
        CHECK(entityData.size() == 1);

        const bool unsaved = world::mergeSectorDataLayers(sector->dataLayers, fileLayers);
        sector->dirty = unsaved;
        // finalizeSectorLoad rebuilds entityUUIDs from the parsed payload before the sector counts
        // as Loaded again; without this the sector would be "loaded" with no entities, which is
        // precisely the state canMutateDataLayers exists to keep out of saveSector.
        sector->entityUUIDs.push_back(920090);
        sector->state = world::SectorState::Loaded;

        REQUIRE(sector->dataLayers.contains("fogOfWar"));
        CHECK(sector->dataLayers.at("fogOfWar") == std::vector<uint8_t>{0x01, 0x02, 0x03});
        CHECK_FALSE(sector->dirty); // identical to the file - nothing left to save

        SUBCASE("a layer created after the save survives the reload AND keeps the sector dirty")
        {
            // This is the pair of assertions VK-1596's ACs hang on. The second one fails against
            // an unconditional `dirty = false` in finalizeSectorLoad: the layer would survive the
            // reload but Save World would then skip the sector and never write it.
            sector->dataLayers["resourceGrid"] = std::vector<uint8_t>(64, 0x5A);

            std::vector<nlohmann::json> reloadEntities;
            world::SectorDataLayers reloadFileLayers;
            REQUIRE(world::WorldSectorSerialization::loadSector(path, reloadEntities,
                                                               &reloadFileLayers));

            const bool stillUnsaved =
                world::mergeSectorDataLayers(sector->dataLayers, reloadFileLayers);

            CHECK(sector->dataLayers.contains("fogOfWar"));    // came back from the file
            CHECK(sector->dataLayers.contains("resourceGrid")); // survived on the struct
            CHECK(stillUnsaved);
        }

        SUBCASE("removing a layer only becomes permanent once the sector is saved")
        {
            // The merge is try_emplace, so an in-memory removal is a hole the file fills. The tab
            // says so on screen rather than pretending otherwise.
            sector->dataLayers.erase("fogOfWar");

            std::vector<nlohmann::json> reloadEntities;
            world::SectorDataLayers reloadFileLayers;
            REQUIRE(world::WorldSectorSerialization::loadSector(path, reloadEntities,
                                                               &reloadFileLayers));
            world::SectorDataLayers resurrected = sector->dataLayers;
            CHECK_FALSE(world::mergeSectorDataLayers(resurrected, reloadFileLayers));
            CHECK(resurrected.contains("fogOfWar"));

            // Save first, and it is gone for good.
            sector->dirty = true;
            REQUIRE(world::WorldSectorSerialization::saveSector(*sector, path));

            std::vector<nlohmann::json> afterSave;
            world::SectorDataLayers afterSaveLayers;
            REQUIRE(world::WorldSectorSerialization::loadSector(path, afterSave, &afterSaveLayers));
            CHECK_FALSE(afterSaveLayers.contains("fogOfWar"));
            // ...and the save carried the sector's entities through, rather than truncating them
            CHECK(afterSave.size() == 1);
        }
    }

    TEST_CASE("VK-1596: a zero-byte data layer round-trips as a zero-byte layer")
    {
        // The Create button writes an empty blob, so this pins that an empty payload survives the
        // msgpack section: json::binary({}) must stay binary, or the reader's is_binary() check
        // drops the layer and Create would appear to do nothing.
        resetTestRoot();
        TestEntities scope{{920091, {5.0f, 0.0f, 5.0f}}};

        auto sector = makeSector({920091});
        sector.dataLayers["emptyLayer"] = {};

        const std::string path = (testRoot() / "empty_layer.vfsector").string();
        REQUIRE(world::WorldSectorSerialization::saveSector(sector, path));

        std::vector<nlohmann::json> entityData;
        world::SectorDataLayers layers;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, entityData, &layers));

        REQUIRE(layers.contains("emptyLayer"));
        CHECK(layers.at("emptyLayer").empty());
    }
}
