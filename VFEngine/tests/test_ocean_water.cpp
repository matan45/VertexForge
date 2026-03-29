#include <doctest.h>
#include <water/WaterTileGrid.hpp>
#include <water/OceanSerializer.hpp>
#include <terrain/TerrainTypes.hpp>

// ============================================================
// VK-1088: Ocean & Water unit tests
// ============================================================

TEST_SUITE("OceanWater") {

// ---- WaterTileGrid ----

TEST_CASE("WaterTileGrid: add and has tile") {
    water::WaterTileGrid grid;

    terrain::TileCoord coord(3, 5);
    CHECK_FALSE(grid.hasTile(coord));

    grid.addTile(coord, 10.0f);
    CHECK(grid.hasTile(coord));
}

TEST_CASE("WaterTileGrid: tileCount tracks additions") {
    water::WaterTileGrid grid;
    CHECK(grid.tileCount() == 0);

    grid.addTile(terrain::TileCoord(0, 0), 5.0f);
    CHECK(grid.tileCount() == 1);

    grid.addTile(terrain::TileCoord(1, 0), 5.0f);
    CHECK(grid.tileCount() == 2);
}

TEST_CASE("WaterTileGrid: removeTile") {
    water::WaterTileGrid grid;
    terrain::TileCoord coord(2, 3);

    grid.addTile(coord, 10.0f);
    REQUIRE(grid.hasTile(coord));

    grid.removeTile(coord);
    CHECK_FALSE(grid.hasTile(coord));
    CHECK(grid.tileCount() == 0);
}

TEST_CASE("WaterTileGrid: clear removes all tiles") {
    water::WaterTileGrid grid;

    grid.addTile(terrain::TileCoord(0, 0), 1.0f);
    grid.addTile(terrain::TileCoord(1, 1), 2.0f);
    grid.addTile(terrain::TileCoord(2, 2), 3.0f);
    REQUIRE(grid.tileCount() == 3);

    grid.clear();
    CHECK(grid.tileCount() == 0);
    CHECK_FALSE(grid.hasTile(terrain::TileCoord(0, 0)));
}

TEST_CASE("WaterTileGrid: duplicate tile updates height, count unchanged") {
    water::WaterTileGrid grid;
    terrain::TileCoord coord(5, 5);

    grid.addTile(coord, 10.0f);
    CHECK(grid.tileCount() == 1);

    // Adding same coord again should update, not duplicate
    grid.addTile(coord, 20.0f);
    CHECK(grid.tileCount() == 1);
    CHECK(grid.hasTile(coord));
}

TEST_CASE("WaterTileGrid: remove nonexistent tile is safe") {
    water::WaterTileGrid grid;
    // Should not crash or change state
    grid.removeTile(terrain::TileCoord(99, 99));
    CHECK(grid.tileCount() == 0);
}

// ---- OceanFileData defaults ----

TEST_CASE("OceanFileData: MAX_BANDS equals 3") {
    CHECK(ocean::OceanFileData::MAX_BANDS == 3);
}

TEST_CASE("OceanFileData: default band hierarchy") {
    ocean::OceanFileData data;

    SUBCASE("all bands enabled by default") {
        for (uint32_t i = 0; i < ocean::OceanFileData::MAX_BANDS; ++i) {
            CHECK(data.bands[i].enabled);
        }
    }

    SUBCASE("patch sizes decrease across bands (far to near detail)") {
        CHECK(data.bands[0].patchSize > data.bands[1].patchSize);
        CHECK(data.bands[1].patchSize > data.bands[2].patchSize);
    }

    SUBCASE("band resolutions are positive powers of two") {
        for (uint32_t i = 0; i < ocean::OceanFileData::MAX_BANDS; ++i) {
            CHECK(data.bands[i].resolution > 0);
            CHECK((data.bands[i].resolution & (data.bands[i].resolution - 1)) == 0);
        }
    }
}

TEST_CASE("OceanFileData: physics defaults are sensible") {
    ocean::OceanFileData data;

    SUBCASE("density is positive") {
        CHECK(data.density > 0.0f);
    }

    SUBCASE("drag is non-negative") {
        CHECK(data.drag >= 0.0f);
    }

    SUBCASE("buoyancy strength is positive") {
        CHECK(data.buoyancyStrength > 0.0f);
    }

    SUBCASE("gravity is positive") {
        CHECK(data.gravity > 0.0f);
    }
}

TEST_CASE("OceanFileData: visual defaults are valid ranges") {
    ocean::OceanFileData data;

    CHECK(data.maxVisibleDepth > 0.0f);
    CHECK(data.fresnelPower > 0.0f);
    CHECK(data.shoreFoamRange > 0.0f);
    CHECK(data.shoreFoamIntensity >= 0.0f);
    CHECK(data.shoreFoamIntensity <= 1.0f);
}

} // TEST_SUITE("OceanWater")
