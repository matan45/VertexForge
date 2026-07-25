#include <doctest.h>
#include <water/WaterTileGrid.hpp>
#include <water/OceanSerializer.hpp>
#include <water/DisplacementSampling.hpp>
#include <terrain/TerrainTypes.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

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

// ---- VK-1604 ----

TEST_CASE("OceanFileData: VK-1604 visual features default to OFF") {
    ocean::OceanFileData data;

    // Every new feature must be opt-in so scenes authored before VK-1604 look unchanged.
    CHECK_FALSE(data.ssrEnabled);
    CHECK_FALSE(data.ssrDebugView);
    CHECK_FALSE(data.beerLambertEnabled);
    CHECK_FALSE(data.hexTilingEnabled);

    CHECK(data.ssrMaxSteps > 0u);
    CHECK(data.ssrMaxDistance > 0.0f);
    CHECK(data.ssrThickness > 0.0f);
    CHECK(data.absorptionMaxDistance > 0.0f);

    // Clear-water absorption: red extinguishes fastest, blue slowest. This ordering is what
    // makes deep water read as blue, and it is mirrored by postprocess/underwater.glsl.
    CHECK(data.absorptionCoeff.r > data.absorptionCoeff.g);
    CHECK(data.absorptionCoeff.g > data.absorptionCoeff.b);

    // Swell (band 0) is off by default — it is the dominant height contributor and therefore
    // the most expensive band to tile on both the GPU and the CPU buoyancy path.
    CHECK((data.hexBandMask & 0x1u) == 0u);
    CHECK((data.hexBandMask & 0x2u) != 0u);
    CHECK((data.hexBandMask & 0x4u) != 0u);
    CHECK(data.hexCellScale > 0.0f);
    CHECK(data.hexBlendContrast >= 1.0f);
}

TEST_CASE("OceanSerializer: .vfOcean round-trip preserves every visual field") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_ocean_vk1604.vfOcean";

    ocean::OceanFileData out;
    out.waterHeight = 12.5f;
    out.physicsEnabled = false;
    out.shallowColor = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
    out.deepColor = glm::vec4(0.5f, 0.6f, 0.7f, 0.8f);
    out.maxVisibleDepth = 17.0f;
    out.fresnelPower = 3.25f;
    out.refractionStrength = 0.75f;
    out.refractionChromatic = 0.125f;
    out.refractionDepthScale = 0.375f;
    out.causticStrength = 1.75f;
    out.causticDepthFalloff = 0.625f;
    out.shoreFoamRange = 6.5f;
    out.shoreFoamIntensity = 0.55f;
    out.shoreBreakingStrength = 1.25f;
    out.shoreWetRange = 7.5f;
    out.shoreWetDarkening = 0.45f;
    out.shoreWetRoughness = 0.35f;

    out.ssrEnabled = true;
    out.ssrIntensity = 0.65f;
    out.ssrMaxDistance = 123.0f;
    out.ssrThickness = 0.85f;
    out.ssrMaxSteps = 47u;
    out.ssrDebugView = true;
    out.beerLambertEnabled = true;
    out.absorptionCoeff = glm::vec3(0.11f, 0.22f, 0.33f);
    out.scatteringColor = glm::vec3(0.44f, 0.55f, 0.66f);
    out.scatterCoeff = 0.077f;
    out.absorptionMaxDistance = 42.0f;
    out.hexTilingEnabled = true;
    out.hexBandMask = 0x7u;
    out.hexCellScale = 2.5f;
    out.hexBlendContrast = 6.5f;
    // VK-1605
    out.shoalingEnabled = true;
    out.shoalingStrength = 0.85f;
    out.shoalingMinDepth = 0.45f;
    out.shoalingWavelengthScale = 1.75f;
    out.shoalingGamma = 0.66f;
    out.shoreEdgeFadeStart = 0.72f;
    out.shoreWavesEnabled = true;
    out.shoreWaveAmplitude = 0.95f;
    out.shoreWaveLength = 17.5f;
    out.shoreWaveSpeed = 0.55f;
    out.shoreWaveBreakDepth = 2.25f;
    out.shoreWaveBreakRange = 1.75f;
    out.shoreWaveCrestFoam = 0.85f;
    out.shoreWaveCrestFoamThreshold = 0.35f;
    out.shoreWaveLean = 1.15f;

    REQUIRE(ocean::OceanSerializer::save(tmp.string(), out));

    ocean::OceanFileData in;
    REQUIRE(ocean::OceanSerializer::load(tmp.string(), in));

    CHECK(in.waterHeight == doctest::Approx(12.5f));
    CHECK(in.physicsEnabled == false);
    CHECK(in.shallowColor.a == doctest::Approx(0.4f));
    CHECK(in.deepColor.r == doctest::Approx(0.5f));
    CHECK(in.maxVisibleDepth == doctest::Approx(17.0f));
    CHECK(in.fresnelPower == doctest::Approx(3.25f));
    CHECK(in.refractionStrength == doctest::Approx(0.75f));
    CHECK(in.refractionChromatic == doctest::Approx(0.125f));
    CHECK(in.refractionDepthScale == doctest::Approx(0.375f));
    CHECK(in.causticStrength == doctest::Approx(1.75f));
    CHECK(in.causticDepthFalloff == doctest::Approx(0.625f));
    CHECK(in.shoreFoamRange == doctest::Approx(6.5f));
    CHECK(in.shoreFoamIntensity == doctest::Approx(0.55f));
    CHECK(in.shoreBreakingStrength == doctest::Approx(1.25f));
    CHECK(in.shoreWetRange == doctest::Approx(7.5f));
    CHECK(in.shoreWetDarkening == doctest::Approx(0.45f));
    CHECK(in.shoreWetRoughness == doctest::Approx(0.35f));

    CHECK(in.ssrEnabled == true);
    CHECK(in.ssrIntensity == doctest::Approx(0.65f));
    CHECK(in.ssrMaxDistance == doctest::Approx(123.0f));
    CHECK(in.ssrThickness == doctest::Approx(0.85f));
    CHECK(in.ssrMaxSteps == 47u);
    CHECK(in.ssrDebugView == true);
    CHECK(in.beerLambertEnabled == true);
    CHECK(in.absorptionCoeff.r == doctest::Approx(0.11f));
    CHECK(in.absorptionCoeff.g == doctest::Approx(0.22f));
    CHECK(in.absorptionCoeff.b == doctest::Approx(0.33f));
    CHECK(in.scatteringColor.r == doctest::Approx(0.44f));
    CHECK(in.scatteringColor.g == doctest::Approx(0.55f));
    CHECK(in.scatteringColor.b == doctest::Approx(0.66f));
    CHECK(in.scatterCoeff == doctest::Approx(0.077f));
    CHECK(in.absorptionMaxDistance == doctest::Approx(42.0f));
    CHECK(in.hexTilingEnabled == true);
    CHECK(in.hexBandMask == 0x7u);
    CHECK(in.hexCellScale == doctest::Approx(2.5f));
    CHECK(in.hexBlendContrast == doctest::Approx(6.5f));

    // VK-1605
    CHECK(in.shoalingEnabled == true);
    CHECK(in.shoalingStrength == doctest::Approx(0.85f));
    CHECK(in.shoalingMinDepth == doctest::Approx(0.45f));
    CHECK(in.shoalingWavelengthScale == doctest::Approx(1.75f));
    CHECK(in.shoalingGamma == doctest::Approx(0.66f));
    CHECK(in.shoreEdgeFadeStart == doctest::Approx(0.72f));
    CHECK(in.shoreWavesEnabled == true);
    CHECK(in.shoreWaveAmplitude == doctest::Approx(0.95f));
    CHECK(in.shoreWaveLength == doctest::Approx(17.5f));
    CHECK(in.shoreWaveSpeed == doctest::Approx(0.55f));
    CHECK(in.shoreWaveBreakDepth == doctest::Approx(2.25f));
    CHECK(in.shoreWaveBreakRange == doctest::Approx(1.75f));
    CHECK(in.shoreWaveCrestFoam == doctest::Approx(0.85f));
    CHECK(in.shoreWaveCrestFoamThreshold == doctest::Approx(0.35f));
    CHECK(in.shoreWaveLean == doctest::Approx(1.15f));

    fs::remove(tmp);
}

TEST_CASE("OceanSerializer: a v1 file without the VK-1604 keys loads with defaults") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_ocean_v1.vfOcean";

    // Hand-written v1 payload: has a "visual" object, but none of the VK-1604 keys.
    // load() never branches on "version" — every key is read with a self-defaulting value(),
    // so this is the actual compatibility mechanism and deserves a direct test.
    {
        std::ofstream file(tmp);
        REQUIRE(file.is_open());
        file << R"({
    "version": 1,
    "waterHeight": 3.0,
    "physicsEnabled": true,
    "visual": {
        "maxVisibleDepth": 9.0,
        "fresnelPower": 4.0
    }
})";
    }

    ocean::OceanFileData in;
    REQUIRE(ocean::OceanSerializer::load(tmp.string(), in));

    CHECK(in.waterHeight == doctest::Approx(3.0f));
    CHECK(in.maxVisibleDepth == doctest::Approx(9.0f));
    CHECK(in.fresnelPower == doctest::Approx(4.0f));

    const ocean::OceanFileData defaults;
    CHECK(in.ssrEnabled == defaults.ssrEnabled);
    CHECK(in.ssrIntensity == doctest::Approx(defaults.ssrIntensity));
    CHECK(in.ssrMaxSteps == defaults.ssrMaxSteps);
    CHECK(in.beerLambertEnabled == defaults.beerLambertEnabled);
    CHECK(in.absorptionCoeff.r == doctest::Approx(defaults.absorptionCoeff.r));
    CHECK(in.scatteringColor.g == doctest::Approx(defaults.scatteringColor.g));
    CHECK(in.scatterCoeff == doctest::Approx(defaults.scatterCoeff));
    CHECK(in.absorptionMaxDistance == doctest::Approx(defaults.absorptionMaxDistance));
    CHECK(in.hexTilingEnabled == defaults.hexTilingEnabled);
    CHECK(in.hexBandMask == defaults.hexBandMask);
    CHECK(in.hexCellScale == doctest::Approx(defaults.hexCellScale));
    CHECK(in.hexBlendContrast == doctest::Approx(defaults.hexBlendContrast));
    // VK-1605 — same mechanism, and defaulting to OFF is what keeps existing scenes unchanged.
    CHECK(in.shoalingEnabled == defaults.shoalingEnabled);
    CHECK(in.shoalingStrength == doctest::Approx(defaults.shoalingStrength));
    CHECK(in.shoalingGamma == doctest::Approx(defaults.shoalingGamma));
    CHECK(in.shoreWavesEnabled == defaults.shoreWavesEnabled);
    CHECK(in.shoreWaveAmplitude == doctest::Approx(defaults.shoreWaveAmplitude));
    CHECK(in.shoreWaveBreakDepth == doctest::Approx(defaults.shoreWaveBreakDepth));

    fs::remove(tmp);
}

// ---- VK-1604: DisplacementSampling (CPU mirror of the shader's repeat-bilinear fetch) ----

TEST_CASE("DisplacementSampling: wrapTexel implements GL_REPEAT") {
    CHECK(water::wrapTexel(0, 4) == 0u);
    CHECK(water::wrapTexel(3, 4) == 3u);
    CHECK(water::wrapTexel(4, 4) == 0u);
    CHECK(water::wrapTexel(5, 4) == 1u);
    CHECK(water::wrapTexel(-1, 4) == 3u);
    CHECK(water::wrapTexel(-4, 4) == 0u);
    CHECK(water::wrapTexel(-5, 4) == 3u);
}

TEST_CASE("DisplacementSampling: bilinear on a known 2x2 grid") {
    // Texel centres sit at UV 0.25 and 0.75 for a 2x2 grid.
    const std::vector<float> grid = {0.0f, 1.0f,
                                      2.0f, 3.0f};
    auto fetch = [&grid](uint32_t i) { return grid[i]; };

    CHECK(water::sampleBilinearWrapped(fetch, 2, {0.25f, 0.25f}) == doctest::Approx(0.0f));
    CHECK(water::sampleBilinearWrapped(fetch, 2, {0.75f, 0.25f}) == doctest::Approx(1.0f));
    CHECK(water::sampleBilinearWrapped(fetch, 2, {0.25f, 0.75f}) == doctest::Approx(2.0f));
    CHECK(water::sampleBilinearWrapped(fetch, 2, {0.75f, 0.75f}) == doctest::Approx(3.0f));

    // Halfway between the two columns of row 0.
    CHECK(water::sampleBilinearWrapped(fetch, 2, {0.5f, 0.25f}) == doctest::Approx(0.5f));
    // Centre of the grid = mean of all four texels.
    CHECK(water::sampleBilinearWrapped(fetch, 2, {0.5f, 0.5f}) == doctest::Approx(1.5f));
}

TEST_CASE("DisplacementSampling: sampling is periodic in UV") {
    const std::vector<float> grid = {0.0f, 1.0f, 2.0f, 3.0f,
                                      4.0f, 5.0f, 6.0f, 7.0f,
                                      8.0f, 9.0f, 10.0f, 11.0f,
                                      12.0f, 13.0f, 14.0f, 15.0f};
    auto fetch = [&grid](uint32_t i) { return grid[i]; };

    // The FFT patch repeats, so u and u+n must sample identically.
    for (float u : {0.0f, 0.13f, 0.5f, 0.87f})
    {
        const float base = water::sampleBilinearWrapped(fetch, 4, {u, 0.31f});
        CHECK(water::sampleBilinearWrapped(fetch, 4, {u + 1.0f, 0.31f}) == doctest::Approx(base));
        CHECK(water::sampleBilinearWrapped(fetch, 4, {u - 3.0f, 0.31f}) == doctest::Approx(base));
        CHECK(water::sampleBilinearWrapped(fetch, 4, {u + 8.0f, 0.31f}) == doctest::Approx(base));
    }

    // Large offsets matter: world coordinates divided by patch size grow without bound as the
    // camera travels. Only exact binary fractions are used here - at u = 512.13 a float has
    // ~3e-5 of fractional resolution left, so a non-exact u would drift for reasons that have
    // nothing to do with the wrap logic under test.
    for (float u : {0.0f, 0.25f, 0.5f, 0.75f})
    {
        const float base = water::sampleBilinearWrapped(fetch, 4, {u, 0.5f});
        CHECK(water::sampleBilinearWrapped(fetch, 4, {u + 512.0f, 0.5f}) == doctest::Approx(base));
        CHECK(water::sampleBilinearWrapped(fetch, 4, {u - 1024.0f, 0.5f}) == doctest::Approx(base));
    }
}

TEST_CASE("DisplacementSampling: patchUV mirrors worldPos.xz / patchSize") {
    CHECK(water::patchUV({250.0f, 125.0f}, 500.0f).x == doctest::Approx(0.5f));
    CHECK(water::patchUV({250.0f, 125.0f}, 500.0f).y == doctest::Approx(0.25f));

    // Degenerate patch size must not divide by zero.
    CHECK(water::patchUV({1.0f, 1.0f}, 0.0f).x == doctest::Approx(0.0f));
    CHECK(water::patchUV({1.0f, 1.0f}, -5.0f).y == doctest::Approx(0.0f));
}

// VK-1607 review finding #12: patchUV's uv = (0,0) substitute is NOT the same thing as "no height".
// Sampling a real grid at (0,0) returns a real texel, and it returns the SAME one for every world
// position - a constant offset surface that buoyancy reads as the water level.
TEST_CASE("DisplacementSampling: a degenerate patch size has no height, not a constant one") {
    // Deliberately non-zero at UV (0,0) so the guard is the only thing that can produce 0.
    const std::vector<float> grid = {5.0f, 6.0f,
                                      7.0f, 8.0f};
    auto fetch = [&grid](uint32_t i) { return grid[i]; };

    SUBCASE("a valid patch size samples normally") {
        CHECK(water::sampleBandHeight(fetch, 2, {0.0f, 0.0f}, 4.0f) ==
              doctest::Approx(water::sampleBilinearWrapped(fetch, 2, water::patchUV({0.0f, 0.0f}, 4.0f))));
    }

    SUBCASE("a non-positive patch size is exactly zero everywhere") {
        CHECK(water::sampleBandHeight(fetch, 2, {0.0f, 0.0f}, 0.0f) == 0.0f);
        CHECK(water::sampleBandHeight(fetch, 2, {123.0f, -456.0f}, 0.0f) == 0.0f);
        CHECK(water::sampleBandHeight(fetch, 2, {0.0f, 0.0f}, -5.0f) == 0.0f);

        // What it would have returned without the guard - proof the guard is load-bearing.
        CHECK(water::sampleBilinearWrapped(fetch, 2, water::patchUV({123.0f, -456.0f}, 0.0f)) != 0.0f);
    }
}

} // TEST_SUITE("OceanWater")
