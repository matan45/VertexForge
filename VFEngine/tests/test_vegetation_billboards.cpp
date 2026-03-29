#include <doctest.h>
#include <vegetation/VegetationTypes.hpp>
#include <vegetation/VegetationSpatialGrid.hpp>

// ============================================================
// VK-1096: Vegetation & Billboards unit tests
// ============================================================

TEST_SUITE("VegetationBillboards") {

// ---- VegetationSpatialGrid: insert and queryRadius ----

TEST_CASE("VegetationSpatialGrid: insert then queryRadius finds nearby instances") {
    vegetation::VegetationSpatialGrid grid;
    grid.setCellSize(2.0f);

    grid.insert(0, glm::vec3(5.0f, 0.0f, 5.0f));
    grid.insert(1, glm::vec3(6.0f, 0.0f, 5.0f));
    grid.insert(2, glm::vec3(100.0f, 0.0f, 100.0f));

    auto results = grid.queryRadius(glm::vec3(5.5f, 0.0f, 5.0f), 3.0f);
    CHECK(results.size() == 2);

    // The far-away instance should not be found
    auto farResults = grid.queryRadius(glm::vec3(5.5f, 0.0f, 5.0f), 1.0f);
    // Both instance 0 and 1 are within 1.0 of (5.5, 0, 5)
    CHECK(farResults.size() == 2);

    auto noResults = grid.queryRadius(glm::vec3(50.0f, 0.0f, 50.0f), 1.0f);
    CHECK(noResults.empty());
}

TEST_CASE("VegetationSpatialGrid: queryRadius returns empty on empty grid") {
    vegetation::VegetationSpatialGrid grid;
    auto results = grid.queryRadius(glm::vec3(0.0f), 10.0f);
    CHECK(results.empty());
}

TEST_CASE("VegetationSpatialGrid: queryRadius uses XZ distance (ignores Y)") {
    vegetation::VegetationSpatialGrid grid;
    grid.setCellSize(5.0f);

    // Same XZ position, different Y
    grid.insert(0, glm::vec3(0.0f, 100.0f, 0.0f));

    auto results = grid.queryRadius(glm::vec3(0.0f, 0.0f, 0.0f), 1.0f);
    // Should find it since XZ distance is 0
    CHECK(results.size() == 1);
}

// ---- VegetationSpatialGrid: hasNeighborWithin ----

TEST_CASE("VegetationSpatialGrid: hasNeighborWithin true when close") {
    vegetation::VegetationSpatialGrid grid;
    grid.setCellSize(2.0f);

    grid.insert(0, glm::vec3(10.0f, 0.0f, 10.0f));

    CHECK(grid.hasNeighborWithin(glm::vec3(10.5f, 0.0f, 10.0f), 1.0f));
}

TEST_CASE("VegetationSpatialGrid: hasNeighborWithin false when far") {
    vegetation::VegetationSpatialGrid grid;
    grid.setCellSize(2.0f);

    grid.insert(0, glm::vec3(10.0f, 0.0f, 10.0f));

    CHECK_FALSE(grid.hasNeighborWithin(glm::vec3(20.0f, 0.0f, 20.0f), 1.0f));
}

TEST_CASE("VegetationSpatialGrid: hasNeighborWithin false on empty grid") {
    vegetation::VegetationSpatialGrid grid;
    CHECK_FALSE(grid.hasNeighborWithin(glm::vec3(0.0f), 5.0f));
}

// ---- VegetationSpatialGrid: rebuild from instances ----

TEST_CASE("VegetationSpatialGrid: rebuild populates grid from instances") {
    vegetation::VegetationSpatialGrid grid;
    grid.setCellSize(2.0f);

    std::vector<vegetation::BillboardInstance> instances;
    instances.push_back({glm::vec3(1.0f, 0.0f, 1.0f), 0.0f, 1.0f, 0, 0.0f});
    instances.push_back({glm::vec3(2.0f, 0.0f, 2.0f), 0.0f, 1.0f, 0, 0.0f});
    instances.push_back({glm::vec3(50.0f, 0.0f, 50.0f), 0.0f, 1.0f, 0, 0.0f});

    grid.rebuild(instances);

    auto near = grid.queryRadius(glm::vec3(1.5f, 0.0f, 1.5f), 2.0f);
    CHECK(near.size() == 2);

    auto far = grid.queryRadius(glm::vec3(50.0f, 0.0f, 50.0f), 1.0f);
    CHECK(far.size() == 1);
}

// ---- BillboardMode enum values ----

TEST_CASE("BillboardMode: Cross is 0, CameraFacing is 1") {
    CHECK(static_cast<uint8_t>(vegetation::BillboardMode::Cross) == 0);
    CHECK(static_cast<uint8_t>(vegetation::BillboardMode::CameraFacing) == 1);
}

// ---- VegetationBrushParams defaults ----

TEST_CASE("VegetationBrushParams: default radius is positive") {
    vegetation::VegetationBrushParams params;
    CHECK(params.radius > 0.0f);
    CHECK(params.radius == doctest::Approx(5.0f));
}

TEST_CASE("VegetationBrushParams: default spacing is positive") {
    vegetation::VegetationBrushParams params;
    CHECK(params.spacing > 0.0f);
    CHECK(params.spacing == doctest::Approx(0.5f));
}

TEST_CASE("VegetationBrushParams: default density is positive") {
    vegetation::VegetationBrushParams params;
    CHECK(params.density > 0.0f);
}

TEST_CASE("VegetationBrushParams: default positionJitter in [0,1]") {
    vegetation::VegetationBrushParams params;
    CHECK(params.positionJitter >= 0.0f);
    CHECK(params.positionJitter <= 1.0f);
}

// ---- BillboardPaletteEntry defaults ----

TEST_CASE("BillboardPaletteEntry: default scale range min < max") {
    vegetation::BillboardPaletteEntry entry;
    CHECK(entry.scaleRange.x < entry.scaleRange.y);
    CHECK(entry.scaleRange.x > 0.0f);
}

TEST_CASE("BillboardPaletteEntry: default mode is Cross") {
    vegetation::BillboardPaletteEntry entry;
    CHECK(entry.mode == vegetation::BillboardMode::Cross);
}

} // TEST_SUITE
