#include <doctest.h>

#include "terrain/BrushFalloff.hpp"
#include "vegetation/VegetationSpatialGrid.hpp"
#include "vegetation/VegetationSerializer.hpp"
#include "data/VegetationUndoCommands.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vegetation/GrassEvents.hpp"

#include <filesystem>
#include <map>
#include <utility>

// ============================================================
// Grass brush expansion unit tests (falloff, masks, serialization, undo)
// ============================================================

TEST_SUITE("GrassBrush") {

// ---- Shared brush falloff curve ----

TEST_CASE("applyFalloff: Constant is full influence everywhere") {
    CHECK(terrain::applyFalloff(0.0f, terrain::BrushFalloff::Constant) == doctest::Approx(1.0f));
    CHECK(terrain::applyFalloff(1.0f, terrain::BrushFalloff::Constant) == doctest::Approx(1.0f));
}

TEST_CASE("applyFalloff: Linear ramps 1 -> 0") {
    CHECK(terrain::applyFalloff(0.0f, terrain::BrushFalloff::Linear) == doctest::Approx(1.0f));
    CHECK(terrain::applyFalloff(0.5f, terrain::BrushFalloff::Linear) == doctest::Approx(0.5f));
    CHECK(terrain::applyFalloff(1.0f, terrain::BrushFalloff::Linear) == doctest::Approx(0.0f));
}

TEST_CASE("applyFalloff: all curves are full at center and zero at edge") {
    for (auto f : {terrain::BrushFalloff::Linear, terrain::BrushFalloff::Smooth,
                   terrain::BrushFalloff::Sharp})
    {
        CHECK(terrain::applyFalloff(0.0f, f) == doctest::Approx(1.0f));
        CHECK(terrain::applyFalloff(1.0f, f) == doctest::Approx(0.0f));
    }
}

TEST_CASE("applyFalloff: thinning is monotonically decreasing for Smooth") {
    float prev = terrain::applyFalloff(0.0f, terrain::BrushFalloff::Smooth);
    for (float t = 0.1f; t <= 1.0f; t += 0.1f)
    {
        float v = terrain::applyFalloff(t, terrain::BrushFalloff::Smooth);
        CHECK(v <= prev + 1e-5f);
        prev = v;
    }
}

// ---- Layer-aware spatial grid (C4) ----

TEST_CASE("VegetationSpatialGrid: hasNeighborOfOtherLayer ignores same layer") {
    vegetation::VegetationSpatialGrid grid;
    grid.setCellSize(2.0f);
    grid.insert(0, glm::vec3(10.0f, 0.0f, 10.0f), /*layer*/ 1);

    // Same layer (1) -> not considered "other"
    CHECK_FALSE(grid.hasNeighborOfOtherLayer(glm::vec3(10.2f, 0.0f, 10.0f), 1.0f, 1));
    // Different layer (0) -> the layer-1 instance counts as "other"
    CHECK(grid.hasNeighborOfOtherLayer(glm::vec3(10.2f, 0.0f, 10.0f), 1.0f, 0));
}

TEST_CASE("VegetationSpatialGrid: countWithin counts only inside radius") {
    vegetation::VegetationSpatialGrid grid;
    grid.setCellSize(2.0f);
    grid.insert(0, glm::vec3(0.0f, 0.0f, 0.0f));
    grid.insert(1, glm::vec3(0.5f, 0.0f, 0.0f));
    grid.insert(2, glm::vec3(20.0f, 0.0f, 0.0f));

    CHECK(grid.countWithin(glm::vec3(0.0f), 1.0f) == 2);
    CHECK(grid.countWithin(glm::vec3(0.0f), 100.0f) == 3);
}

TEST_CASE("VegetationSpatialGrid: rebuild preserves per-instance layer") {
    vegetation::VegetationSpatialGrid grid;
    grid.setCellSize(2.0f);

    std::vector<vegetation::BillboardInstance> instances(2);
    instances[0].position = glm::vec3(1.0f, 0.0f, 1.0f);
    instances[0].paletteEntryIndex = 3;
    instances[1].position = glm::vec3(1.2f, 0.0f, 1.0f);
    instances[1].paletteEntryIndex = 3;
    grid.rebuild(instances);

    // A query from a different layer must see these as "other layer"
    CHECK(grid.hasNeighborOfOtherLayer(glm::vec3(1.1f, 0.0f, 1.0f), 1.0f, 0));
    CHECK_FALSE(grid.hasNeighborOfOtherLayer(glm::vec3(1.1f, 0.0f, 1.0f), 1.0f, 3));
}

// ---- Serialization round-trip for new per-instance fields (B2/B3/C3) ----

TEST_CASE("VegetationSerializer: instance round-trip preserves height/tint/normal") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_instances.vfvi";

    std::vector<vegetation::BillboardInstance> out(1);
    out[0].position = glm::vec3(3.0f, 4.0f, 5.0f);
    out[0].rotation = 1.2f;
    out[0].scale = 0.75f;
    out[0].paletteEntryIndex = 2;
    out[0].heightScale = 1.4f;
    out[0].tint = 0.8f;
    out[0].normal = glm::normalize(glm::vec3(0.1f, 0.95f, 0.2f));

    REQUIRE(vegetation::VegetationSerializer::saveBillboardInstances(tmp.string(), out));

    std::vector<vegetation::BillboardInstance> in;
    REQUIRE(vegetation::VegetationSerializer::loadBillboardInstances(tmp.string(), in));
    REQUIRE(in.size() == 1);
    CHECK(in[0].scale == doctest::Approx(0.75f));
    CHECK(in[0].paletteEntryIndex == 2);
    CHECK(in[0].heightScale == doctest::Approx(1.4f));
    CHECK(in[0].tint == doctest::Approx(0.8f));
    CHECK(in[0].normal.y == doctest::Approx(out[0].normal.y));

    fs::remove(tmp);
}

TEST_CASE("VegetationSerializer: palette round-trip preserves heightRange/tintJitter") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_palette.json";

    std::vector<vegetation::BillboardPaletteEntry> out(1);
    out[0].texturePath = "grass.vfImage";
    out[0].heightRange = glm::vec2(0.7f, 1.6f);
    out[0].tintJitter = 0.35f;

    REQUIRE(vegetation::VegetationSerializer::saveBillboardPalette(tmp.string(), out));

    std::vector<vegetation::BillboardPaletteEntry> in;
    REQUIRE(vegetation::VegetationSerializer::loadBillboardPalette(tmp.string(), in));
    REQUIRE(in.size() == 1);
    CHECK(in[0].heightRange.x == doctest::Approx(0.7f));
    CHECK(in[0].heightRange.y == doctest::Approx(1.6f));
    CHECK(in[0].tintJitter == doctest::Approx(0.35f));

    fs::remove(tmp);
}

// ---- Stroke undo command (A7) ----

TEST_CASE("VegetationTileSnapshotUndoCommand: execute/undo drive tile state") {
    auto& dispatcher = events::EventDispatcher::instance();
    dispatcher.clear();

    std::map<std::pair<int, int>, std::vector<vegetation::BillboardInstance>> store;
    dispatcher.registerCommandHandler<events::vegetation::SetTileBillboardInstancesCommand>(
        [&store](const events::vegetation::SetTileBillboardInstancesCommand& cmd)
        {
            store[{cmd.tileX, cmd.tileZ}] = cmd.instances;
        });

    std::vector<vegetation::BillboardInstance> before; // empty before the stroke
    std::vector<vegetation::BillboardInstance> after(3); // stroke painted 3

    services::VegetationTileSnapshotUndoCommand cmd("Vegetation Brush");
    cmd.addTile(0, 0, before, after);
    CHECK(cmd.hasChanges());

    cmd.execute(); // redo / apply painted state
    CHECK(store[{0, 0}].size() == 3);

    cmd.undo(); // restore pre-stroke state
    CHECK(store[{0, 0}].size() == 0);

    dispatcher.clear();
}

} // TEST_SUITE
