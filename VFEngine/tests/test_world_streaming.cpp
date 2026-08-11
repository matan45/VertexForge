#include <doctest.h>
#include <world/WorldTypes.hpp>
#include <world/HLODTypes.hpp>
#include <world/WorldSectorManager.hpp>

#include <limits>
#include <unordered_set>

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

// ---- VK-1588: sectorCoordToId range ----

TEST_CASE("sectorCoordToId: unique across the validated coord range") {
    // Decode locally rather than adding a production inverse nobody calls: the id is bit
    // concatenation, biased x in the high 16 bits, biased z in the low 16.
    auto decodeX = [](uint32_t id) { return static_cast<int32_t>((id >> 16) & 0xFFFFu) - 0x8000; };
    auto decodeZ = [](uint32_t id) { return static_cast<int32_t>(id & 0xFFFFu) - 0x8000; };

    // Corners, one step inside each, and interior points. The full range is 2^32 pairs - a
    // brute-force sweep is not the point, injectivity plus exact round-trip is.
    const int32_t samples[] = {
        world::kMinSectorCoord, world::kMinSectorCoord + 1, -4096, -1, 0, 1, 4096,
        world::kMaxSectorCoord - 1, world::kMaxSectorCoord
    };

    std::unordered_set<uint32_t> seen;
    for (int32_t x : samples) {
        for (int32_t z : samples) {
            world::SectorCoord coord(x, z);
            REQUIRE(world::isValidSectorCoord(coord));

            uint32_t id = world::sectorCoordToId(coord);
            CHECK(seen.insert(id).second);  // no two in-range coords share an id
            CHECK(decodeX(id) == x);        // and the packing round-trips exactly
            CHECK(decodeZ(id) == z);
        }
    }
}

TEST_CASE("isValidSectorCoord: accepts the packable range, rejects outside it") {
    CHECK(world::isValidSectorCoord({0, 0}));
    CHECK(world::isValidSectorCoord({world::kMinSectorCoord, world::kMaxSectorCoord}));
    CHECK(world::isValidSectorCoord({world::kMaxSectorCoord, world::kMinSectorCoord}));

    CHECK_FALSE(world::isValidSectorCoord({world::kMinSectorCoord - 1, 0}));
    CHECK_FALSE(world::isValidSectorCoord({world::kMaxSectorCoord + 1, 0}));
    CHECK_FALSE(world::isValidSectorCoord({0, world::kMinSectorCoord - 1}));
    CHECK_FALSE(world::isValidSectorCoord({0, world::kMaxSectorCoord + 1}));

    CHECK_FALSE(world::isValidSectorCoord({std::numeric_limits<int32_t>::min(), 0}));
    CHECK_FALSE(world::isValidSectorCoord({std::numeric_limits<int32_t>::max(), 0}));
}

TEST_CASE("sectorCoordToId: coords one 16-bit period apart alias - why the range is bounded") {
    // The exact failure the range guard exists to prevent.
    CHECK(world::sectorCoordToId({3, 5}) == world::sectorCoordToId({3, 5 + 0x10000}));
    CHECK_FALSE(world::isValidSectorCoord({3, 5 + 0x10000}));
}

TEST_CASE("sectorCoordToId: extreme coords are defined, not UB") {
    // Biasing used to be `static_cast<uint32_t>(coord.x + 0x4000)` - signed overflow near the
    // int32 limits. The cast now happens before the bias, so these are merely wrong, not UB.
    const int32_t lo = std::numeric_limits<int32_t>::min();
    const int32_t hi = std::numeric_limits<int32_t>::max();
    CHECK(world::sectorCoordToId({lo, hi}) == world::sectorCoordToId({lo, hi}));
    CHECK(world::sectorCoordToId({hi, lo}) == world::sectorCoordToId({hi, lo}));
}

// ---- VK-1588: SectorStreamingConfig defaults ----

TEST_CASE("SectorStreamingConfig: default-constructed values") {
    world::SectorStreamingConfig config;

    SUBCASE("radii are sector counts with hysteresis") {
        CHECK(config.loadRadius == doctest::Approx(4.0f));
        CHECK(config.unloadRadius == doctest::Approx(5.0f));
        CHECK(config.unloadRadius > config.loadRadius);
        // Regression guard: these are SECTOR COUNTS, not world units. The old serializer
        // fallback of 512/640 was a TerrainWorldStreamer copy-paste.
        CHECK(config.loadRadius < 32.0f);
    }
    SUBCASE("VK-1591: prefetchRadius defaults to the 0 sentinel, not an absolute radius") {
        // A literal default (e.g. 4.0f) would silently grow a prefetch ring for every config
        // that sets loadRadius without mentioning prefetchRadius — which is all of them.
        CHECK(config.prefetchRadius == doctest::Approx(0.0f));
        CHECK(world::effectivePrefetchRadius(config) == doctest::Approx(config.loadRadius));

        world::SectorStreamingConfig tighter;
        tighter.loadRadius = 2.0f;
        CHECK(world::effectivePrefetchRadius(tighter) == doctest::Approx(2.0f));

        CHECK(config.maxPrefetchBytes == 0); // 0 = unlimited
    }
    SUBCASE("per-frame budgets are conservative") {
        CHECK(config.maxLoadsPerFrame == 1);
        CHECK(config.maxPrefetchesPerFrame == 1);
        CHECK(config.maxUnloadsPerFrame == 1);
        CHECK(config.maxEntitiesPerFrame == 8);
        CHECK(config.maxTerrainLoadsPerFrame == 4);
        CHECK(config.maxTerrainUnloadsPerFrame == 4);
    }
    SUBCASE("flags") {
        CHECK(config.enableGPUObjectStreaming == true);
        CHECK(config.editModeStreaming == false);
    }
}

// ---- VK-1588: coord derivation clamps instead of casting into UB ----

TEST_CASE("worldPositionToSectorCoord clamps garbage input into the addressable range") {
    world::SectorConfig config;
    config.sectorWorldSize = 128.0f;
    world::WorldSectorManager manager(config);

    SUBCASE("finite in-range position is unchanged") {
        auto coord = manager.worldPositionToSectorCoord({256.0f, 0.0f, -384.0f});
        CHECK(coord.x == 2);
        CHECK(coord.z == -3);
    }
    SUBCASE("NaN collapses to sector 0 instead of casting into UB") {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        auto coord = manager.worldPositionToSectorCoord({nan, 0.0f, nan});
        CHECK(coord.x == 0);
        CHECK(coord.z == 0);
    }
    SUBCASE("huge positions clamp to the range bounds") {
        auto coord = manager.worldPositionToSectorCoord({1e30f, 0.0f, -1e30f});
        CHECK(coord.x == world::kMaxSectorCoord);
        CHECK(coord.z == world::kMinSectorCoord);
        CHECK(world::isValidSectorCoord(coord));
    }
    SUBCASE("zero sectorWorldSize does not divide into garbage") {
        world::SectorConfig zeroed;
        zeroed.sectorWorldSize = 0.0f;
        world::WorldSectorManager degenerate(zeroed);
        auto coord = degenerate.worldPositionToSectorCoord({10.0f, 0.0f, 10.0f});
        CHECK(world::isValidSectorCoord(coord));
        CHECK(coord.x == 0);
        CHECK(coord.z == 0);
    }
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
