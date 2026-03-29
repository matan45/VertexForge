#include <doctest.h>
#include <world/WorldTypes.hpp>
#include <world/HLODTypes.hpp>

// ============================================================
// VK-1086: World & Streaming unit tests
// ============================================================

TEST_SUITE("WorldStreaming") {

// ---- SectorCoord arithmetic ----

TEST_CASE("SectorCoord addition") {
    world::SectorCoord a(3, 5);
    world::SectorCoord b(2, -1);
    auto result = a + b;

    CHECK(result.x == 5);
    CHECK(result.z == 4);
}

TEST_CASE("SectorCoord subtraction") {
    world::SectorCoord a(10, 7);
    world::SectorCoord b(3, 2);
    auto result = a - b;

    CHECK(result.x == 7);
    CHECK(result.z == 5);
}

TEST_CASE("SectorCoord equality") {
    world::SectorCoord a(1, 2);
    world::SectorCoord b(1, 2);
    world::SectorCoord c(1, 3);

    CHECK(a == b);
    CHECK_FALSE(a == c);
}

TEST_CASE("SectorCoord inequality") {
    world::SectorCoord a(1, 2);
    world::SectorCoord b(3, 4);

    CHECK(a != b);
    CHECK_FALSE(a != a);
}

// ---- SectorCoordHash ----

TEST_CASE("SectorCoordHash: different coords produce different hashes") {
    world::SectorCoordHash hasher;

    auto h00 = hasher(world::SectorCoord(0, 0));
    auto h10 = hasher(world::SectorCoord(1, 0));
    auto h01 = hasher(world::SectorCoord(0, 1));
    auto h11 = hasher(world::SectorCoord(1, 1));
    auto hNeg = hasher(world::SectorCoord(-1, -1));

    // Not all hashes should collide for these simple distinct coords
    CHECK(h00 != h10);
    CHECK(h00 != h01);
    CHECK(h10 != h01);
    CHECK(h11 != hNeg);
}

// ---- sectorCoordToId ----

TEST_CASE("sectorCoordToId: different coords produce different IDs") {
    auto id1 = world::sectorCoordToId(world::SectorCoord(0, 0));
    auto id2 = world::sectorCoordToId(world::SectorCoord(1, 0));
    auto id3 = world::sectorCoordToId(world::SectorCoord(0, 1));

    CHECK(id1 != id2);
    CHECK(id1 != id3);
    CHECK(id2 != id3);
}

TEST_CASE("sectorCoordToId: negative coords work") {
    // Should not throw or produce the same ID as positive mirror
    auto idPos = world::sectorCoordToId(world::SectorCoord(1, 1));
    auto idNeg = world::sectorCoordToId(world::SectorCoord(-1, -1));

    CHECK(idPos != idNeg);
}

TEST_CASE("sectorCoordToId: same coord gives same ID") {
    auto id1 = world::sectorCoordToId(world::SectorCoord(5, -3));
    auto id2 = world::sectorCoordToId(world::SectorCoord(5, -3));

    CHECK(id1 == id2);
}

// ---- alignSectorConfigToTerrain ----

TEST_CASE("alignSectorConfigToTerrain: sectorWorldSize == tileSize * tilesPerSector") {
    float tileSize = 32.0f;
    int32_t tilesPerSector = 4;

    auto config = world::alignSectorConfigToTerrain(tileSize, tilesPerSector);

    CHECK(config.sectorWorldSize == doctest::Approx(tileSize * static_cast<float>(tilesPerSector)));
    CHECK(config.tilesPerSector == tilesPerSector);
    CHECK(config.alignedToTerrain == true);
}

TEST_CASE("alignSectorConfigToTerrain: different tile sizes") {
    auto config = world::alignSectorConfigToTerrain(64.0f, 2);
    CHECK(config.sectorWorldSize == doctest::Approx(128.0f));
}

// ---- isSectorAlignedToTerrain ----

TEST_CASE("isSectorAlignedToTerrain: true for matching config") {
    auto config = world::alignSectorConfigToTerrain(32.0f, 4);
    CHECK(world::isSectorAlignedToTerrain(config, 32.0f));
}

TEST_CASE("isSectorAlignedToTerrain: false for mismatching config") {
    world::SectorConfig config;
    config.sectorWorldSize = 100.0f;
    config.tilesPerSector = 4;

    CHECK_FALSE(world::isSectorAlignedToTerrain(config, 32.0f));
}

// ---- HLODCellCoord ----

TEST_CASE("HLODCellCoord equality") {
    world::HLODCellCoord a(1, 2, 0);
    world::HLODCellCoord b(1, 2, 0);
    world::HLODCellCoord c(1, 2, 1);

    CHECK(a == b);
    CHECK_FALSE(a == c);
}

TEST_CASE("HLODCellCoord inequality") {
    world::HLODCellCoord a(1, 2, 0);
    world::HLODCellCoord b(3, 4, 0);

    CHECK(a != b);
    CHECK_FALSE(a != a);
}

// ---- HLODCellCoordHash ----

TEST_CASE("HLODCellCoordHash: different coords produce different hashes") {
    world::HLODCellCoordHash hasher;

    auto h1 = hasher(world::HLODCellCoord(0, 0, 0));
    auto h2 = hasher(world::HLODCellCoord(1, 0, 0));
    auto h3 = hasher(world::HLODCellCoord(0, 1, 0));
    auto h4 = hasher(world::HLODCellCoord(0, 0, 1));

    CHECK(h1 != h2);
    CHECK(h1 != h3);
    CHECK(h1 != h4);
}

// ---- HLODConfig ----

TEST_CASE("HLODConfig::defaultConfig returns valid tier hierarchy") {
    auto config = world::HLODConfig::defaultConfig();

    SUBCASE("has 3 tiers") {
        REQUIRE(config.tiers.size() == 3);
    }

    SUBCASE("tiers are ordered by tier index") {
        CHECK(config.tiers[0].tier == 0);
        CHECK(config.tiers[1].tier == 1);
        CHECK(config.tiers[2].tier == 2);
    }

    SUBCASE("display radius increases with tier") {
        CHECK(config.tiers[0].displayRadius < config.tiers[1].displayRadius);
        CHECK(config.tiers[1].displayRadius < config.tiers[2].displayRadius);
    }

    SUBCASE("cell size increases with tier") {
        CHECK(config.tiers[0].cellSize <= config.tiers[1].cellSize);
        CHECK(config.tiers[1].cellSize <= config.tiers[2].cellSize);
    }

    SUBCASE("simplification ratio decreases with tier") {
        CHECK(config.tiers[0].simplificationRatio > config.tiers[1].simplificationRatio);
        CHECK(config.tiers[1].simplificationRatio > config.tiers[2].simplificationRatio);
    }
}

} // TEST_SUITE("WorldStreaming")
