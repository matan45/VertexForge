#include <doctest.h>
#include <world/WorldSector.hpp>
#include <world/WorldSectorManager.hpp>
#include <world/WorldSectorSerialization.hpp>
#include <world/WorldDefinition.hpp>
#include <world/WorldDefinitionSerialization.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
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
