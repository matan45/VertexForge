#include <doctest.h>
#include <terrain/TerrainCompression.hpp>
#include <terrain/TerrainWeightMap.hpp>
#include <terrain/CaveSDFData.hpp>
#include <terrain/TerrainTypes.hpp>
#include <glm/glm.hpp>
#include <cmath>
#include <vector>

// ============================================================
// VK-1087: Terrain utility unit tests
// ============================================================

TEST_SUITE("Terrain") {

// ---- Height compression ----

TEST_CASE("compression::computeHeightRange returns min/max") {
    std::vector<float> heights = {5.0f, -3.0f, 10.0f, 0.0f, 7.5f};
    auto params = terrain::compression::computeHeightRange(heights);

    CHECK(params.minH == doctest::Approx(-3.0f));
    CHECK(params.maxH == doctest::Approx(10.0f));
}

TEST_CASE("compression::computeHeightRange on empty vector") {
    std::vector<float> heights;
    auto params = terrain::compression::computeHeightRange(heights);

    CHECK(params.minH == doctest::Approx(0.0f));
    CHECK(params.maxH == doctest::Approx(0.0f));
}

TEST_CASE("compression::computeHeightRange single element") {
    std::vector<float> heights = {42.0f};
    auto params = terrain::compression::computeHeightRange(heights);

    CHECK(params.minH == doctest::Approx(42.0f));
    CHECK(params.maxH == doctest::Approx(42.0f));
}

TEST_CASE("compression: quantize/dequantize heights roundtrip") {
    std::vector<float> original = {0.0f, 25.0f, 50.0f, 75.0f, 100.0f};
    auto params = terrain::compression::computeHeightRange(original);
    auto quantized = terrain::compression::quantizeHeights(original, params);
    auto restored = terrain::compression::dequantizeHeights(quantized, params);

    REQUIRE(restored.size() == original.size());

    float maxError = 100.0f / 65535.0f; // range / 65535
    for (size_t i = 0; i < original.size(); ++i) {
        CHECK(restored[i] == doctest::Approx(original[i]).epsilon(maxError));
    }
}

TEST_CASE("compression: quantize/dequantize heights with negative values") {
    std::vector<float> original = {-50.0f, -10.0f, 0.0f, 30.0f, 80.0f};
    auto params = terrain::compression::computeHeightRange(original);
    auto quantized = terrain::compression::quantizeHeights(original, params);
    auto restored = terrain::compression::dequantizeHeights(quantized, params);

    REQUIRE(restored.size() == original.size());

    float range = params.maxH - params.minH;
    float maxError = range / 65535.0f;
    for (size_t i = 0; i < original.size(); ++i) {
        CHECK(std::abs(restored[i] - original[i]) < maxError + 0.001f);
    }
}

TEST_CASE("compression: flat tile quantization") {
    std::vector<float> flat = {5.0f, 5.0f, 5.0f};
    auto params = terrain::compression::computeHeightRange(flat);
    auto quantized = terrain::compression::quantizeHeights(flat, params);
    auto restored = terrain::compression::dequantizeHeights(quantized, params);

    for (size_t i = 0; i < flat.size(); ++i) {
        CHECK(restored[i] == doctest::Approx(5.0f));
    }
}

// ---- Weight compression ----

TEST_CASE("compression: quantize/dequantize weights roundtrip stays in [0,1]") {
    std::vector<float> original = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    auto quantized = terrain::compression::quantizeWeights(original);
    auto restored = terrain::compression::dequantizeWeights(quantized);

    REQUIRE(restored.size() == original.size());

    for (size_t i = 0; i < restored.size(); ++i) {
        CHECK(restored[i] >= 0.0f);
        CHECK(restored[i] <= 1.0f);
        CHECK(restored[i] == doctest::Approx(original[i]).epsilon(1.0f / 255.0f));
    }
}

TEST_CASE("compression: weight quantization clamps out-of-range") {
    std::vector<float> clamped = {-0.5f, 1.5f};
    auto quantized = terrain::compression::quantizeWeights(clamped);
    auto restored = terrain::compression::dequantizeWeights(quantized);

    CHECK(restored[0] == doctest::Approx(0.0f));
    CHECK(restored[1] == doctest::Approx(1.0f));
}

// ---- TileWeightMapData ----

TEST_CASE("TileWeightMapData::initializeDefault") {
    terrain::TileWeightMapData wm;
    wm.initializeDefault(33);

    SUBCASE("isInitialized returns true") {
        CHECK(wm.isInitialized());
    }

    SUBCASE("getTexelCount matches resolution squared") {
        CHECK(wm.getTexelCount() == 33u * 33u);
    }

    SUBCASE("channel 0 has weight 1.0, others 0.0") {
        CHECK(wm.getWeight(0, 0, 0) == doctest::Approx(1.0f));
        CHECK(wm.getWeight(1, 0, 0) == doctest::Approx(0.0f));
        CHECK(wm.getWeight(7, 0, 0) == doctest::Approx(0.0f));
    }
}

TEST_CASE("TileWeightMapData::normalizeAt sums to 1.0") {
    terrain::TileWeightMapData wm;
    wm.initializeDefault(33);

    // Set some non-normalized weights at (0,0)
    wm.setWeight(0, 0, 0, 3.0f);
    wm.setWeight(1, 0, 0, 2.0f);
    wm.setWeight(2, 0, 0, 5.0f);

    wm.normalizeAt(0, 0);

    float sum = 0.0f;
    for (uint32_t ch = 0; ch < terrain::WEIGHT_CHANNELS; ++ch) {
        sum += wm.getWeight(ch, 0, 0);
    }
    CHECK(sum == doctest::Approx(1.0f));
}

TEST_CASE("TileWeightMapData::assignChannel returns valid index") {
    terrain::TileWeightMapData wm;
    wm.initializeDefault(33);

    uint8_t ch = wm.assignChannel(10);
    CHECK(ch < terrain::WEIGHT_CHANNELS);

    SUBCASE("assigning same palette layer returns same channel") {
        uint8_t ch2 = wm.assignChannel(10);
        CHECK(ch2 == ch);
    }
}

// ---- CaveSDFConfig ----

TEST_CASE("CaveSDFConfig::totalVoxels equals resX * resY * resZ") {
    terrain::CaveSDFConfig cfg;
    cfg.resX = 10;
    cfg.resY = 20;
    cfg.resZ = 30;
    CHECK(cfg.totalVoxels() == 10u * 20u * 30u);
}

TEST_CASE("CaveSDFConfig::fromTileConfig produces matching voxel size") {
    terrain::TerrainTileConfig tileConfig;
    tileConfig.resolution = terrain::TileResolution::Low; // 33 vertices, 32 quads
    tileConfig.worldTileSize = 32.0f;
    tileConfig.maxHeight = 100.0f;
    tileConfig.minHeight = -10.0f;

    auto sdfCfg = terrain::CaveSDFConfig::fromTileConfig(tileConfig);

    SUBCASE("resX matches tile vertex count") {
        CHECK(sdfCfg.resX == tileConfig.getVertexCount());
    }

    SUBCASE("resZ matches tile vertex count") {
        CHECK(sdfCfg.resZ == tileConfig.getVertexCount());
    }

    SUBCASE("voxelSize matches vertex spacing") {
        CHECK(sdfCfg.voxelSize == doctest::Approx(tileConfig.getVertexSpacing()));
    }

    SUBCASE("yVoxelSize equals voxelSize for uniform grid") {
        CHECK(sdfCfg.yVoxelSize == doctest::Approx(sdfCfg.voxelSize));
    }
}

// ---- CaveSDFData ----

TEST_CASE("CaveSDFData::getIndex roundtrip") {
    terrain::CaveSDFData data;
    data.config.resX = 10;
    data.config.resY = 10;
    data.config.resZ = 10;
    data.sdfGrid.resize(data.config.totalVoxels(), -1.0f);

    // Write at specific coords and read back via getIndex
    uint32_t tx = 3, ty = 5, tz = 7;
    size_t idx = data.getIndex(tx, ty, tz);
    data.sdfGrid[idx] = 42.0f;

    CHECK(data.getSDF(tx, ty, tz) == doctest::Approx(42.0f));
}

TEST_CASE("CaveSDFData::hasCaveGeometry false on unmodified data") {
    terrain::CaveSDFData data;
    terrain::TerrainTileConfig tileConfig;
    tileConfig.resolution = terrain::TileResolution::Low;
    tileConfig.worldTileSize = 32.0f;
    tileConfig.maxHeight = 50.0f;
    tileConfig.minHeight = -10.0f;

    // Create flat heightmap
    uint32_t vc = tileConfig.getVertexCount();
    std::vector<float> heightData(static_cast<size_t>(vc) * vc, 10.0f);

    data.initializeFromHeightData(tileConfig, glm::vec3(0.0f), heightData);

    // Original and current SDF are identical - no cave geometry
    CHECK_FALSE(data.hasCaveGeometry());
}

TEST_CASE("CaveSDFData::hasCaveGeometry true after modification") {
    terrain::CaveSDFData data;
    terrain::TerrainTileConfig tileConfig;
    tileConfig.resolution = terrain::TileResolution::Low;
    tileConfig.worldTileSize = 32.0f;
    tileConfig.maxHeight = 50.0f;
    tileConfig.minHeight = -10.0f;

    uint32_t vc = tileConfig.getVertexCount();
    std::vector<float> heightData(static_cast<size_t>(vc) * vc, 10.0f);

    data.initializeFromHeightData(tileConfig, glm::vec3(0.0f), heightData);

    // Carve: flip a solid voxel to air
    data.setSDF(5, 2, 5, 5.0f);

    CHECK(data.hasCaveGeometry());
}

TEST_CASE("CaveSDFData::computeGradient returns normalized vector") {
    terrain::CaveSDFData data;
    terrain::TerrainTileConfig tileConfig;
    tileConfig.resolution = terrain::TileResolution::Low;
    tileConfig.worldTileSize = 32.0f;
    tileConfig.maxHeight = 50.0f;
    tileConfig.minHeight = -10.0f;

    uint32_t vc = tileConfig.getVertexCount();
    std::vector<float> heightData(static_cast<size_t>(vc) * vc, 10.0f);

    data.initializeFromHeightData(tileConfig, glm::vec3(0.0f), heightData);

    // Pick an interior voxel so gradient has meaningful neighbors
    glm::vec3 grad = data.computeGradient(5, 5, 5);
    float len = glm::length(grad);

    CHECK(len == doctest::Approx(1.0f).epsilon(0.001f));
}

} // TEST_SUITE("Terrain")
