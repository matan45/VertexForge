#include <doctest.h>
#include <types/NavmeshTypes.hpp>
#include <navigation/NavmeshData.hpp>
#include <glm/glm.hpp>

// ============================================================
// VK-1100: NavMesh / Pathfinding unit tests
// ============================================================

TEST_SUITE("NavMeshPathfinding") {

// ---- NavmeshBakeSettings ----

TEST_CASE("NavmeshBakeSettings: default values are sensible") {
    types::NavmeshBakeSettings settings;
    CHECK(settings.cellSize > 0.0f);
    CHECK(settings.cellHeight > 0.0f);
    CHECK(settings.agentRadius > 0.0f);
    CHECK(settings.agentHeight > 0.0f);
    CHECK(settings.agentMaxClimb > 0.0f);
    CHECK(settings.agentMaxSlope > 0.0f);
    CHECK(settings.agentMaxSlope <= 90.0f);
}

TEST_CASE("NavmeshBakeSettings: agent parameters valid ranges") {
    types::NavmeshBakeSettings settings;
    CHECK(settings.agentRadius < settings.agentHeight);
    CHECK(settings.agentMaxClimb < settings.agentHeight);
}

TEST_CASE("NavmeshBakeSettings: tile size positive") {
    types::NavmeshBakeSettings settings;
    CHECK(settings.tileSize > 0);
}

TEST_CASE("NavmeshBakeSettings: region sizes positive") {
    types::NavmeshBakeSettings settings;
    CHECK(settings.regionMinSize > 0);
    CHECK(settings.regionMergeSize > 0);
    CHECK(settings.regionMergeSize > settings.regionMinSize);
}

// ---- NavmeshTileCoord ----

TEST_CASE("NavmeshTileCoord: equality") {
    navigation::NavmeshTileCoord a{1, 2};
    navigation::NavmeshTileCoord b{1, 2};
    navigation::NavmeshTileCoord c{3, 4};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("NavmeshTileCoordHash: different coords produce different hashes") {
    navigation::NavmeshTileCoordHash hasher;
    navigation::NavmeshTileCoord a{0, 0};
    navigation::NavmeshTileCoord b{1, 0};
    navigation::NavmeshTileCoord c{0, 1};
    CHECK(hasher(a) != hasher(b));
    CHECK(hasher(a) != hasher(c));
    CHECK(hasher(b) != hasher(c));
}

// ---- NavmeshInputGeometry ----

TEST_CASE("NavmeshInputGeometry: starts empty") {
    navigation::NavmeshInputGeometry geom;
    CHECK(geom.isEmpty());
    CHECK(geom.getVertexCount() == 0);
    CHECK(geom.getTriangleCount() == 0);
}

TEST_CASE("NavmeshInputGeometry: add vertices and triangles") {
    navigation::NavmeshInputGeometry geom;
    geom.addVertex({0, 0, 0});
    geom.addVertex({1, 0, 0});
    geom.addVertex({0, 0, 1});
    geom.addTriangle(0, 1, 2);

    CHECK(geom.getVertexCount() == 3);
    CHECK(geom.getTriangleCount() == 1);
    CHECK_FALSE(geom.isEmpty());
}

// ---- NavmeshFileHeader ----

TEST_CASE("NavmeshFileHeader: magic and version") {
    navigation::NavmeshFileHeader header;
    CHECK(header.magic == navigation::NAVMESH_FILE_MAGIC);
    CHECK(header.version == navigation::NAVMESH_FILE_VERSION);
}

// ---- NavPath ----

TEST_CASE("NavPath: default is invalid") {
    navigation::NavPath path;
    CHECK_FALSE(path.isValid);
    CHECK(path.waypoints.empty());
}

// ---- NavmeshLodConfig ----

TEST_CASE("NavmeshLodConfig: LOD distances increasing") {
    types::NavmeshLodConfig config;
    CHECK(config.lodDistances[0] < config.lodDistances[1]);
    CHECK(config.lodDistances[1] < config.lodDistances[2]);
}

TEST_CASE("NavmeshLodConfig: cell size multipliers increasing") {
    types::NavmeshLodConfig config;
    CHECK(config.cellSizeMultipliers[0] < config.cellSizeMultipliers[1]);
    CHECK(config.cellSizeMultipliers[1] < config.cellSizeMultipliers[2]);
}

// ---- Area names ----

TEST_CASE("getNavmeshAreaName: known areas return non-null") {
    CHECK(types::getNavmeshAreaName(types::NAVMESH_AREA_GROUND) != nullptr);
    CHECK(types::getNavmeshAreaName(types::NAVMESH_AREA_WATER) != nullptr);
}

} // TEST_SUITE
