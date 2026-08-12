#include <doctest.h>
#include <world/WorldSector.hpp>
#include <world/WorldSectorManager.hpp>
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
        definition.sectorConfig.sectorWorldSize = 256.0f;
        definition.sectorConfig.tilesPerSector = 8;
        definition.streamingConfig.loadRadius = 6.0f;
        definition.streamingConfig.unloadRadius = 9.0f;
        definition.streamingConfig.maxLoadsPerFrame = 3;
        definition.streamingConfig.maxEntitiesPerFrame = 16;
        definition.streamingConfig.editModeStreaming = true;
        definition.sectorFilePaths[{1, -2}] = "sectors/sector_1_-2.vfsector";

        std::string path = (testRoot() / "roundtrip.vfworld").string();
        REQUIRE(world::WorldDefinitionSerialization::save(definition, path));

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));
        CHECK(loaded.name == "RoundTrip");
        CHECK(loaded.sectorConfig.sectorWorldSize == doctest::Approx(256.0f));
        CHECK(loaded.sectorConfig.tilesPerSector == 8);
        CHECK(loaded.streamingConfig.loadRadius == doctest::Approx(6.0f));
        CHECK(loaded.streamingConfig.unloadRadius == doctest::Approx(9.0f));
        CHECK(loaded.streamingConfig.maxLoadsPerFrame == 3);
        CHECK(loaded.streamingConfig.maxEntitiesPerFrame == 16);
        CHECK(loaded.streamingConfig.editModeStreaming == true);
        REQUIRE(loaded.sectorFilePaths.size() == 1);
        CHECK(loaded.sectorFilePaths.at({1, -2}) == "sectors/sector_1_-2.vfsector");
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
            CHECK(loaded.sectorConfig.sectorWorldSize == doctest::Approx(sectorDefaults.sectorWorldSize));
            CHECK(loaded.sectorConfig.tilesPerSector == sectorDefaults.tilesPerSector);
            CHECK(loaded.sectorConfig.alignedToTerrain == sectorDefaults.alignedToTerrain);
        }
        SUBCASE("every streamingConfig field falls back to the struct")
        {
            CHECK(loaded.streamingConfig.loadRadius == doctest::Approx(d.loadRadius));
            CHECK(loaded.streamingConfig.unloadRadius == doctest::Approx(d.unloadRadius));
            CHECK(loaded.streamingConfig.maxLoadsPerFrame == d.maxLoadsPerFrame);
            CHECK(loaded.streamingConfig.maxUnloadsPerFrame == d.maxUnloadsPerFrame);
            CHECK(loaded.streamingConfig.maxEntitiesPerFrame == d.maxEntitiesPerFrame);
            CHECK(loaded.streamingConfig.maxTerrainLoadsPerFrame == d.maxTerrainLoadsPerFrame);
            CHECK(loaded.streamingConfig.maxTerrainUnloadsPerFrame == d.maxTerrainUnloadsPerFrame);
            CHECK(loaded.streamingConfig.enableGPUObjectStreaming == d.enableGPUObjectStreaming);
            CHECK(loaded.streamingConfig.editModeStreaming == d.editModeStreaming);
            CHECK(loaded.streamingConfig.hlodTier0Radius == doctest::Approx(d.hlodTier0Radius));
            CHECK(loaded.streamingConfig.hlodTier1Radius == doctest::Approx(d.hlodTier1Radius));
            CHECK(loaded.streamingConfig.hlodTier2Radius == doctest::Approx(d.hlodTier2Radius));
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
        CHECK(loaded.streamingConfig.loadRadius == doctest::Approx(7.0f));
        CHECK(loaded.streamingConfig.unloadRadius == doctest::Approx(d.unloadRadius));
        CHECK(loaded.streamingConfig.maxEntitiesPerFrame == d.maxEntitiesPerFrame);
        CHECK(loaded.streamingConfig.enableGPUObjectStreaming == d.enableGPUObjectStreaming);
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
}
