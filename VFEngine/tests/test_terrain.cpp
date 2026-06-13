#include <doctest.h>
#include <terrain/TerrainCompression.hpp>
#include <terrain/TerrainWeightMap.hpp>
#include <terrain/CaveSDFData.hpp>
#include <terrain/CaveMeshGenerator.hpp>
#include <terrain/TerrainTile.hpp>
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

// ---- Cave Surface Nets mesher (Phase 1) ----

// Build a tile whose cave SDF is a solid block with a spherical air pocket carved
// in the middle, so the Surface Nets isosurface wraps a single closed cave.
static void buildCarvedCaveTile(terrain::TerrainTile& tile) {
    tile.initializeCaveSDF();
    auto& sdf = *tile.caveData;

    // Deep solid everywhere (well below the cave iso level so it reads as solid).
    for (uint32_t z = 0; z < sdf.config.resZ; ++z)
        for (uint32_t y = 0; y < sdf.config.resY; ++y)
            for (uint32_t x = 0; x < sdf.config.resX; ++x)
                sdf.setSDF(x, y, z, -10.0f);

    // Carve an air sphere (positive SDF) in the volume centre.
    glm::vec3 c = sdf.getWorldPosition(sdf.config.resX / 2, sdf.config.resY / 2, sdf.config.resZ / 2);
    float r = 6.0f * sdf.config.voxelSize;
    for (uint32_t z = 0; z < sdf.config.resZ; ++z)
        for (uint32_t y = 0; y < sdf.config.resY; ++y)
            for (uint32_t x = 0; x < sdf.config.resX; ++x) {
                glm::vec3 p = sdf.getWorldPosition(x, y, z);
                float d = glm::length(p - c);
                if (d < r)
                    sdf.setSDF(x, y, z, r - d); // > 0 == air inside the sphere
            }
}

TEST_CASE("cave surface nets: carved sphere produces a non-empty mesh") {
    terrain::TerrainTileConfig config;
    config.resolution = terrain::TileResolution::Low;
    terrain::TerrainTile tile(terrain::TileCoord(0, 0), config);
    buildCarvedCaveTile(tile);

    bool ok = terrain::CaveMeshGenerator::generate(tile);

    CHECK(ok);
    CHECK_FALSE(tile.caveLOD.vertices.empty());
    CHECK_FALSE(tile.caveLOD.indices.empty());
    CHECK(tile.caveLOD.indices.size() % 3 == 0);
    // Every index must reference a real vertex.
    for (uint32_t idx : tile.caveLOD.indices)
        CHECK(idx < tile.caveLOD.vertices.size());
}

TEST_CASE("cave surface nets: no degenerate triangles") {
    terrain::TerrainTileConfig config;
    config.resolution = terrain::TileResolution::Low;
    terrain::TerrainTile tile(terrain::TileCoord(0, 0), config);
    buildCarvedCaveTile(tile);
    REQUIRE(terrain::CaveMeshGenerator::generate(tile));

    const auto& v = tile.caveLOD.vertices;
    const auto& idx = tile.caveLOD.indices;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const glm::vec3& a = v[idx[i]].position;
        const glm::vec3& b = v[idx[i + 1]].position;
        const glm::vec3& c = v[idx[i + 2]].position;
        float area2 = glm::length(glm::cross(b - a, c - a));
        CHECK(area2 > 1e-7f);
    }
}

TEST_CASE("cave surface nets: generation is deterministic") {
    terrain::TerrainTileConfig config;
    config.resolution = terrain::TileResolution::Low;

    terrain::TerrainTile tileA(terrain::TileCoord(0, 0), config);
    terrain::TerrainTile tileB(terrain::TileCoord(0, 0), config);
    buildCarvedCaveTile(tileA);
    buildCarvedCaveTile(tileB);
    REQUIRE(terrain::CaveMeshGenerator::generate(tileA));
    REQUIRE(terrain::CaveMeshGenerator::generate(tileB));

    REQUIRE(tileA.caveLOD.vertices.size() == tileB.caveLOD.vertices.size());
    REQUIRE(tileA.caveLOD.indices.size() == tileB.caveLOD.indices.size());
    for (size_t i = 0; i < tileA.caveLOD.vertices.size(); ++i) {
        CHECK(tileA.caveLOD.vertices[i].position.x == doctest::Approx(tileB.caveLOD.vertices[i].position.x));
        CHECK(tileA.caveLOD.vertices[i].position.y == doctest::Approx(tileB.caveLOD.vertices[i].position.y));
        CHECK(tileA.caveLOD.vertices[i].position.z == doctest::Approx(tileB.caveLOD.vertices[i].position.z));
    }
    for (size_t i = 0; i < tileA.caveLOD.indices.size(); ++i)
        CHECK(tileA.caveLOD.indices[i] == tileB.caveLOD.indices[i]);
}

TEST_CASE("cave surface nets: vertices are finite with normalized normals") {
    terrain::TerrainTileConfig config;
    config.resolution = terrain::TileResolution::Low;
    terrain::TerrainTile tile(terrain::TileCoord(0, 0), config);
    buildCarvedCaveTile(tile);
    REQUIRE(terrain::CaveMeshGenerator::generate(tile));

    for (const auto& vert : tile.caveLOD.vertices) {
        CHECK(std::isfinite(vert.position.x));
        CHECK(std::isfinite(vert.position.y));
        CHECK(std::isfinite(vert.position.z));
        // Tile-local rebase keeps positions near the origin; guard against garbage.
        CHECK(std::abs(vert.position.x) < 1.0e5f);
        CHECK(std::abs(vert.position.y) < 1.0e5f);
        CHECK(std::abs(vert.position.z) < 1.0e5f);
        CHECK(glm::length(vert.normal) == doctest::Approx(1.0f).epsilon(0.01f));
    }
}

TEST_CASE("cave surface nets: all-solid grid yields no geometry") {
    terrain::TerrainTileConfig config;
    config.resolution = terrain::TileResolution::Low;
    terrain::TerrainTile tile(terrain::TileCoord(0, 0), config);
    tile.initializeCaveSDF();
    auto& sdf = *tile.caveData;
    for (uint32_t z = 0; z < sdf.config.resZ; ++z)
        for (uint32_t y = 0; y < sdf.config.resY; ++y)
            for (uint32_t x = 0; x < sdf.config.resX; ++x)
                sdf.setSDF(x, y, z, -10.0f);

    bool ok = terrain::CaveMeshGenerator::generate(tile);
    CHECK_FALSE(ok); // hasCaveGeometry() is false: nothing positive, no surface
    CHECK(tile.caveLOD.vertices.empty());
}

// ---- Cave cross-tile seam apron (Phase 2) ----

// Carve a world-space air sphere into a tile's cave SDF (deterministic in world space,
// so the same call on two adjacent tiles produces identical shared-boundary data).
static void carveWorldSphere(terrain::TerrainTile& tile, glm::vec3 center, float radius) {
    tile.initializeCaveSDF();
    auto& sdf = *tile.caveData;
    for (uint32_t z = 0; z < sdf.config.resZ; ++z)
        for (uint32_t y = 0; y < sdf.config.resY; ++y)
            for (uint32_t x = 0; x < sdf.config.resX; ++x)
                sdf.setSDF(x, y, z, -10.0f);
    for (uint32_t z = 0; z < sdf.config.resZ; ++z)
        for (uint32_t y = 0; y < sdf.config.resY; ++y)
            for (uint32_t x = 0; x < sdf.config.resX; ++x) {
                glm::vec3 p = sdf.getWorldPosition(x, y, z);
                float d = glm::length(p - center);
                if (d < radius)
                    sdf.setSDF(x, y, z, radius - d);
            }
}

TEST_CASE("cave apron: +X neighbour extends the boundary mesh across the seam") {
    terrain::TerrainTileConfig config;
    config.resolution = terrain::TileResolution::Low;

    terrain::TerrainTile a(terrain::TileCoord(0, 0), config);
    terrain::TerrainTile b(terrain::TileCoord(1, 0), config);
    // Shared boundary plane sits at world X == b.worldOrigin.x.
    float seamX = b.worldOrigin.x;
    a.initializeCaveSDF();
    glm::vec3 center(seamX, a.caveData->getWorldPosition(0, a.caveData->config.resY / 2, 0).y,
                     a.worldOrigin.z + 16.0f);

    carveWorldSphere(a, center, 6.0f);
    carveWorldSphere(b, center, 6.0f);

    auto worldMaxX = [](const terrain::TerrainTile& t) {
        float m = -1e30f;
        for (const auto& v : t.caveLOD.vertices)
            m = std::max(m, v.position.x + t.worldOrigin.x);
        return m;
    };

    REQUIRE(terrain::CaveMeshGenerator::generate(a)); // no apron
    float maxNoApron = worldMaxX(a);

    terrain::NeighborCaves nc;
    nc.plusX = b.caveData.get();
    REQUIRE(terrain::CaveMeshGenerator::generate(a, nc)); // with +X apron
    float maxApron = worldMaxX(a);

    // The apron pushes A's mesh ~one voxel past the seam into B's first cell.
    CHECK(maxApron > maxNoApron + 0.5f);
    CHECK(maxApron >= seamX);
}

TEST_CASE("cave apron: adjacent tiles produce coincident seam vertices (no crack)") {
    terrain::TerrainTileConfig config;
    config.resolution = terrain::TileResolution::Low;

    terrain::TerrainTile a(terrain::TileCoord(0, 0), config);
    terrain::TerrainTile b(terrain::TileCoord(1, 0), config);
    float seamX = b.worldOrigin.x;
    a.initializeCaveSDF();
    glm::vec3 center(seamX, a.caveData->getWorldPosition(0, a.caveData->config.resY / 2, 0).y,
                     a.worldOrigin.z + 16.0f);

    carveWorldSphere(a, center, 6.0f);
    carveWorldSphere(b, center, 6.0f);

    terrain::NeighborCaves ncA;
    ncA.plusX = b.caveData.get();
    REQUIRE(terrain::CaveMeshGenerator::generate(a, ncA));
    REQUIRE(terrain::CaveMeshGenerator::generate(b)); // B owns its own interior

    // A's apron vertices that extend past the seam (into B's first cell) must coincide
    // with one of B's vertices — proving the two meshes meet rather than leaving a gap.
    int apronVerts = 0;
    int matched = 0;
    for (const auto& va : a.caveLOD.vertices) {
        glm::vec3 wa(va.position.x + a.worldOrigin.x, va.position.y, va.position.z + a.worldOrigin.z);
        if (wa.x <= seamX + 0.01f)
            continue; // not an apron vertex
        ++apronVerts;
        for (const auto& vb : b.caveLOD.vertices) {
            glm::vec3 wb(vb.position.x + b.worldOrigin.x, vb.position.y, vb.position.z + b.worldOrigin.z);
            if (glm::length(wa - wb) < 1e-2f) { ++matched; break; }
        }
    }
    REQUIRE(apronVerts > 0);      // the carve actually crosses the seam
    CHECK(matched == apronVerts); // every apron vertex coincides with B
}

// ---- Cave / surface junction (Phase 3): iso=0, carved-cells only ----

TEST_CASE("cave junction: sub-surface carve meshes only the pocket, not the flat surface") {
    terrain::TerrainTileConfig config;
    config.resolution = terrain::TileResolution::Low; // flat heightData == 0 from initializeFlat
    terrain::TerrainTile tile(terrain::TileCoord(0, 0), config);

    tile.initializeCaveSDFFromHeights(); // original SDF = flat surface at y == 0
    auto& sdf = *tile.caveData;

    // Carve an enclosed air pocket well below the surface (centre ~y=-10).
    glm::vec3 center = sdf.getWorldPosition(sdf.config.resX / 2, 10, sdf.config.resZ / 2);
    float radius = 4.0f;
    for (uint32_t z = 0; z < sdf.config.resZ; ++z)
        for (uint32_t y = 0; y < sdf.config.resY; ++y)
            for (uint32_t x = 0; x < sdf.config.resX; ++x) {
                glm::vec3 p = sdf.getWorldPosition(x, y, z);
                float d = glm::length(p - center);
                if (d < radius)
                    sdf.setSDF(x, y, z, std::max(sdf.getSDF(x, y, z), radius - d));
            }

    REQUIRE(terrain::CaveMeshGenerator::generate(tile));
    REQUIRE_FALSE(tile.caveLOD.vertices.empty());

    // The carved-cell gate must keep the mesh around the pocket; none of it should sit
    // up at the pristine surface (y == 0). The pocket shell lives near y in [-14,-6].
    float maxY = -1e30f;
    for (const auto& v : tile.caveLOD.vertices)
        maxY = std::max(maxY, v.position.y);
    CHECK(maxY < -2.0f);
}

TEST_CASE("cave normals: point toward the air (cave interior)") {
    terrain::TerrainTileConfig config;
    config.resolution = terrain::TileResolution::Low;
    terrain::TerrainTile tile(terrain::TileCoord(0, 0), config);
    buildCarvedCaveTile(tile); // air sphere in a solid block
    REQUIRE(terrain::CaveMeshGenerator::generate(tile));

    auto& sdf = *tile.caveData;
    glm::vec3 center = sdf.getWorldPosition(sdf.config.resX / 2, sdf.config.resY / 2, sdf.config.resZ / 2);

    // For an air pocket, "toward air" == toward the sphere centre. The shading normal
    // (+gradient) should agree, so a viewer inside sees lit (front) faces.
    int towardAir = 0;
    for (const auto& v : tile.caveLOD.vertices) {
        glm::vec3 worldPos(v.position.x + tile.worldOrigin.x, v.position.y, v.position.z + tile.worldOrigin.z);
        glm::vec3 toCenter = center - worldPos;
        if (glm::dot(toCenter, toCenter) < 1e-6f)
            continue;
        if (glm::dot(v.normal, glm::normalize(toCenter)) > 0.0f)
            ++towardAir;
    }
    // The overwhelming majority must face inward (allow a few noisy shell vertices).
    CHECK(towardAir > static_cast<int>(tile.caveLOD.vertices.size()) * 9 / 10);
}

TEST_CASE("cave junction: uncarved heightmap SDF yields no cave mesh") {
    terrain::TerrainTileConfig config;
    config.resolution = terrain::TileResolution::Low;
    terrain::TerrainTile tile(terrain::TileCoord(0, 0), config);
    tile.initializeCaveSDFFromHeights(); // pristine, nothing carved

    bool ok = terrain::CaveMeshGenerator::generate(tile);
    CHECK_FALSE(ok);
    CHECK(tile.caveLOD.vertices.empty());
}

} // TEST_SUITE("Terrain")
