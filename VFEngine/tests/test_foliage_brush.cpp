#include <doctest.h>

#include <foliage/FoliageScatter.hpp>
#include <foliage/FoliageSpatialGrid.hpp>
#include <foliage/FoliageSerializer.hpp>
#include <foliage/FoliageTypes.hpp>

#include "data/FoliageUndoCommands.hpp"
#include "events/EventDispatcher.hpp"
#include "events/foliage/FoliageEvents.hpp"
#include "impl/foliage/FoliageBrushServiceImpl.hpp"
#include "events/foliage/FoliageBrushEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <vector>
#include <map>
#include <utility>
#include <cstdint>

// ============================================================
// VK-1575: Foliage brush — scatter core + sidecar serialization
// ============================================================

TEST_SUITE("FoliageBrush") {

namespace {
    // A flat terrain at y=0 with an upward normal, backed by a spacing grid so the
    // scatter core exercises spacing rejection just like the live brush.
    foliage::ScatterEnv makeFlatEnv(foliage::FoliageSpatialGrid& grid, float height = 0.0f,
                                    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f))
    {
        foliage::ScatterEnv env;
        env.heightAt = [height](float, float) -> std::optional<float> { return height; };
        env.normalAt = [normal](float, float) -> glm::vec3 { return normal; };
        env.spacingReject = nullptr; // no rejection by default; tests set their own when needed
        env.accept = [&grid](const glm::vec3& p, uint16_t t) { grid.insert(0, p, t); };
        return env;
    }
}

// ---- Case 1: scatter determinism + placement rules ----

TEST_CASE("scatterFoliage is deterministic for a fixed seed") {
    std::vector<foliage::ScatterTypeRule> rules(1);
    rules[0].scaleRange = glm::vec2(1.0f, 2.0f);
    std::vector<uint32_t> enabled = {0};
    foliage::ScatterParams params;
    params.radius = 10.0f;
    params.spacing = 1.0f;
    params.maxCandidates = 64;
    params.applyFalloff = false;

    auto run = [&](uint32_t seed) {
        foliage::FoliageSpatialGrid grid;
        grid.setCellSize(params.spacing);
        foliage::ScatterEnv env = makeFlatEnv(grid);
        env.spacingReject = [&grid, &params](const glm::vec3& p) {
            return grid.hasNeighborWithin(p, params.spacing);
        };
        std::mt19937 rng(seed);
        std::vector<foliage::ScatterCandidate> out;
        foliage::scatterFoliage(glm::vec3(0.0f), rules, enabled, params, env, rng,
                                glm::vec3(0.0f, 1.0f, 0.0f), out);
        return out;
    };

    auto a = run(1234);
    auto b = run(1234);
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() > 0);
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].position.x == doctest::Approx(b[i].position.x));
        CHECK(a[i].position.z == doctest::Approx(b[i].position.z));
        CHECK(a[i].typeIndex == b[i].typeIndex);
        CHECK(a[i].rotationY == doctest::Approx(b[i].rotationY));
        CHECK(a[i].scale.x == doctest::Approx(b[i].scale.x));
        CHECK(a[i].seed == b[i].seed);
    }
}

TEST_CASE("scatterFoliage honors spacing (accepted instances are >= spacing apart)") {
    std::vector<foliage::ScatterTypeRule> rules(1);
    std::vector<uint32_t> enabled = {0};
    foliage::ScatterParams params;
    params.radius = 8.0f;
    params.spacing = 2.0f;
    params.maxCandidates = 200;
    params.applyFalloff = false;

    foliage::FoliageSpatialGrid grid;
    grid.setCellSize(params.spacing);
    foliage::ScatterEnv env = makeFlatEnv(grid);
    env.spacingReject = [&grid, &params](const glm::vec3& p) {
        return grid.hasNeighborWithin(p, params.spacing);
    };
    std::mt19937 rng(7);
    std::vector<foliage::ScatterCandidate> out;
    foliage::scatterFoliage(glm::vec3(0.0f), rules, enabled, params, env, rng,
                            glm::vec3(0.0f, 1.0f, 0.0f), out);

    REQUIRE(out.size() > 1);
    for (size_t i = 0; i < out.size(); ++i)
        for (size_t j = i + 1; j < out.size(); ++j) {
            float dx = out[i].position.x - out[j].position.x;
            float dz = out[i].position.z - out[j].position.z;
            CHECK(std::sqrt(dx * dx + dz * dz) >= params.spacing - 1e-3f);
        }
}

TEST_CASE("scatterFoliage rejects candidates outside the altitude band") {
    std::vector<foliage::ScatterTypeRule> rules(1);
    rules[0].altitudeRange = glm::vec2(100.0f, 200.0f); // flat terrain sits at y=0, excluded
    std::vector<uint32_t> enabled = {0};
    foliage::ScatterParams params;
    params.maxCandidates = 100;
    params.applyFalloff = false;

    foliage::FoliageSpatialGrid grid;
    foliage::ScatterEnv env = makeFlatEnv(grid, /*height*/ 0.0f);
    std::mt19937 rng(11);
    std::vector<foliage::ScatterCandidate> out;
    uint32_t placed = foliage::scatterFoliage(glm::vec3(0.0f), rules, enabled, params, env, rng,
                                              glm::vec3(0.0f, 1.0f, 0.0f), out);
    CHECK(placed == 0);
    CHECK(out.empty());
}

TEST_CASE("scatterFoliage rejects a type whose slope band excludes the surface") {
    std::vector<foliage::ScatterTypeRule> rules(1);
    rules[0].maxSlopeDeg = 10.0f; // only near-flat allowed
    std::vector<uint32_t> enabled = {0};
    foliage::ScatterParams params;
    params.maxCandidates = 100;
    params.applyFalloff = false;

    foliage::FoliageSpatialGrid grid;
    // 45-degree surface normal -> slope 45 deg > 10 deg band.
    glm::vec3 steep = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
    foliage::ScatterEnv env = makeFlatEnv(grid, 0.0f, steep);
    std::mt19937 rng(13);
    std::vector<foliage::ScatterCandidate> out;
    uint32_t placed = foliage::scatterFoliage(glm::vec3(0.0f), rules, enabled, params, env, rng,
                                              steep, out);
    CHECK(placed == 0);
}

TEST_CASE("scatterFoliage weighted pick with a single enabled type paints only that type") {
    std::vector<foliage::ScatterTypeRule> rules(3);
    rules[0].weight = 1.0f;
    rules[1].weight = 1.0f;
    rules[2].weight = 1.0f;
    std::vector<uint32_t> enabled = {2}; // only type 2 paint-enabled
    foliage::ScatterParams params;
    params.radius = 6.0f;
    params.spacing = 0.001f; // effectively no spacing rejection
    params.maxCandidates = 50;
    params.applyFalloff = false;

    foliage::FoliageSpatialGrid grid;
    foliage::ScatterEnv env = makeFlatEnv(grid);
    std::mt19937 rng(21);
    std::vector<foliage::ScatterCandidate> out;
    foliage::scatterFoliage(glm::vec3(0.0f), rules, enabled, params, env, rng,
                            glm::vec3(0.0f, 1.0f, 0.0f), out);
    REQUIRE(out.size() > 0);
    for (const auto& c : out) CHECK(c.typeIndex == 2u);
}

TEST_CASE("scatterFoliage: applyFalloff=false with no rejection accepts every candidate") {
    std::vector<foliage::ScatterTypeRule> rules(1);
    std::vector<uint32_t> enabled = {0};
    foliage::ScatterParams params;
    params.radius = 5.0f;
    params.spacing = 0.001f; // no spacing rejection
    params.maxCandidates = 40;
    params.applyFalloff = false;

    foliage::FoliageSpatialGrid grid;
    foliage::ScatterEnv env = makeFlatEnv(grid);
    env.spacingReject = nullptr; // nothing rejected
    std::mt19937 rng(5);
    std::vector<foliage::ScatterCandidate> out;
    uint32_t placed = foliage::scatterFoliage(glm::vec3(0.0f), rules, enabled, params, env, rng,
                                              glm::vec3(0.0f, 1.0f, 0.0f), out);
    CHECK(placed == params.maxCandidates);
    CHECK(out.size() == params.maxCandidates);
}

// ---- Case 3: FoliageSerializer instance round-trip + magic/version/recordSize guard ----

TEST_CASE("FoliageSerializer: instance round-trip preserves every field (bulk 56B blob)") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_foliage.vffoliage";

    std::vector<foliage::FoliageInstance> out(2);
    out[0].position = glm::vec3(3.0f, 4.0f, 5.0f);
    out[0].rotationY = 1.25f;
    out[0].scale = glm::vec3(1.1f, 2.2f, 1.1f);
    out[0].typeIndex = 7;
    out[0].flags = foliage::FoliageInstanceFlags::Tilt | foliage::FoliageInstanceFlags::Collider;
    out[0].normal = glm::normalize(glm::vec3(0.1f, 0.95f, 0.2f));
    out[0].windPhase = 0.42f;
    out[0].tint = 0x11223344u;
    out[0].seed = 0xDEADBEEFu;
    out[1].position = glm::vec3(-9.0f, 0.0f, 2.0f);
    out[1].typeIndex = 63;

    REQUIRE(foliage::FoliageSerializer::saveFoliageInstances(tmp.string(), out));

    std::vector<foliage::FoliageInstance> in;
    REQUIRE(foliage::FoliageSerializer::loadFoliageInstances(tmp.string(), in));
    REQUIRE(in.size() == 2);
    CHECK(in[0].position.x == doctest::Approx(3.0f));
    CHECK(in[0].position.y == doctest::Approx(4.0f));
    CHECK(in[0].rotationY == doctest::Approx(1.25f));
    CHECK(in[0].scale.y == doctest::Approx(2.2f));
    CHECK(in[0].typeIndex == 7u);
    CHECK(in[0].flags == (foliage::FoliageInstanceFlags::Tilt | foliage::FoliageInstanceFlags::Collider));
    CHECK(in[0].normal.y == doctest::Approx(out[0].normal.y));
    CHECK(in[0].windPhase == doctest::Approx(0.42f));
    CHECK(in[0].tint == 0x11223344u);
    CHECK(in[0].seed == 0xDEADBEEFu);
    CHECK(in[1].typeIndex == 63u);

    fs::remove(tmp);
}

TEST_CASE("FoliageSerializer: empty instance vector round-trips (count == 0)") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_foliage_empty.vffoliage";
    std::vector<foliage::FoliageInstance> out;
    REQUIRE(foliage::FoliageSerializer::saveFoliageInstances(tmp.string(), out));
    std::vector<foliage::FoliageInstance> in{foliage::FoliageInstance{}}; // seed with junk
    REQUIRE(foliage::FoliageSerializer::loadFoliageInstances(tmp.string(), in));
    CHECK(in.empty());
    fs::remove(tmp);
}

TEST_CASE("FoliageSerializer: bad magic is rejected") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_foliage_bad.vffoliage";
    {
        std::ofstream f(tmp, std::ios::binary);
        const char junk[16] = {'X','X','X','X', 1,0,0,0, 56,0,0,0, 0,0,0,0};
        f.write(junk, sizeof(junk));
    }
    std::vector<foliage::FoliageInstance> in;
    CHECK_FALSE(foliage::FoliageSerializer::loadFoliageInstances(tmp.string(), in));
    fs::remove(tmp);
}

// ---- Case 4: FoliageType palette JSON round-trip ----

TEST_CASE("FoliageSerializer: palette JSON round-trip preserves the FoliageType fields") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_foliage_palette.json";

    std::vector<foliage::FoliageType> out(1);
    out[0].meshPath = "tree.vfMesh";
    out[0].materialPath = "bark.vfMatInstance";
    out[0].weight = 2.5f;
    out[0].densityScale = 0.5f;
    out[0].scaleRange = glm::vec2(0.6f, 1.8f);
    out[0].heightRange = glm::vec2(0.9f, 1.3f);
    out[0].rotationYRange = glm::vec2(0.0f, 180.0f);
    out[0].randomTilt = 12.0f;
    out[0].alignToNormal = true;
    out[0].minSlopeDeg = 5.0f;
    out[0].maxSlopeDeg = 40.0f;
    out[0].altitudeRange = glm::vec2(10.0f, 90.0f);
    out[0].startCullDistance = 50.0f;
    out[0].endCullDistance = 350.0f;
    out[0].castShadow = false;
    out[0].farMode = foliage::FoliageFarMode::HLODProxy;
    out[0].receiveWind = true;
    out[0].windStrength = 1.7f;
    out[0].windStiffness = 0.4f;
    out[0].collision = true;
    out[0].navContribute = true;
    out[0].visible = false;
    out[0].paintEnabled = false;

    REQUIRE(foliage::FoliageSerializer::saveFoliagePalette(tmp.string(), out));

    std::vector<foliage::FoliageType> in;
    REQUIRE(foliage::FoliageSerializer::loadFoliagePalette(tmp.string(), in));
    REQUIRE(in.size() == 1);
    CHECK(in[0].meshPath == "tree.vfMesh");
    CHECK(in[0].materialPath == "bark.vfMatInstance");
    CHECK(in[0].weight == doctest::Approx(2.5f));
    CHECK(in[0].densityScale == doctest::Approx(0.5f));
    CHECK(in[0].scaleRange.y == doctest::Approx(1.8f));
    CHECK(in[0].heightRange.x == doctest::Approx(0.9f));
    CHECK(in[0].rotationYRange.y == doctest::Approx(180.0f));
    CHECK(in[0].randomTilt == doctest::Approx(12.0f));
    CHECK(in[0].alignToNormal);
    CHECK(in[0].minSlopeDeg == doctest::Approx(5.0f));
    CHECK(in[0].maxSlopeDeg == doctest::Approx(40.0f));
    CHECK(in[0].altitudeRange.y == doctest::Approx(90.0f));
    CHECK(in[0].endCullDistance == doctest::Approx(350.0f));
    CHECK_FALSE(in[0].castShadow);
    CHECK(in[0].farMode == foliage::FoliageFarMode::HLODProxy);
    CHECK(in[0].receiveWind);
    CHECK(in[0].windStrength == doctest::Approx(1.7f));
    CHECK(in[0].collision);
    CHECK(in[0].navContribute);
    CHECK_FALSE(in[0].visible);
    CHECK_FALSE(in[0].paintEnabled);

    fs::remove(tmp);
}

// ---- Case 2: FoliageTileSnapshotUndoCommand round-trip ----

TEST_CASE("FoliageTileSnapshotUndoCommand: execute/undo drive tile state") {
    auto& dispatcher = events::EventDispatcher::instance();
    dispatcher.clear();

    std::map<std::pair<int, int>, std::vector<foliage::FoliageInstance>> store;
    dispatcher.registerCommandHandler<events::foliage::SetTileFoliageInstancesCommand>(
        [&store](const events::foliage::SetTileFoliageInstancesCommand& cmd)
        {
            store[{cmd.tileX, cmd.tileZ}] = cmd.instances;
        });

    std::vector<foliage::FoliageInstance> before;    // empty before the stroke
    std::vector<foliage::FoliageInstance> after(3);  // stroke painted 3

    services::FoliageTileSnapshotUndoCommand cmd("Foliage Brush");
    cmd.addTile(0, 0, before, after);
    CHECK(cmd.hasChanges());

    cmd.execute(); // redo / apply painted state
    CHECK(store[{0, 0}].size() == 3);

    cmd.undo(); // restore pre-stroke state
    CHECK(store[{0, 0}].size() == 0);

    dispatcher.clear();
}

// ---- Case 5: InstancedFoliage backend emits ZERO CreateEntityCommand ----

TEST_CASE("FoliageBrushServiceImpl paints into tile store, never creating entities") {
    auto& dispatcher = events::EventDispatcher::instance();
    dispatcher.clear();

    uint32_t createEntityCount = 0;
    uint32_t addedInstances = 0;

    // A one-type paint-enabled palette (wide-open masks so candidates survive).
    dispatcher.registerQueryHandler<events::foliage::GetFoliagePaletteQuery>(
        [](const events::foliage::GetFoliagePaletteQuery&) -> std::vector<foliage::FoliageType>
        {
            foliage::FoliageType t;
            t.meshPath = "grass.vfMesh";
            t.paintEnabled = true;
            return {t};
        });
    // Flat terrain at y=0 everywhere.
    dispatcher.registerQueryHandler<events::terrain::GetTerrainHeightAtQuery>(
        [](const events::terrain::GetTerrainHeightAtQuery&) -> terrain::TerrainHeightAtResult
        {
            terrain::TerrainHeightAtResult r;
            r.valid = true;
            r.height = 0.0f;
            return r;
        });
    // Empty tiles (snapshot "before"/"after").
    dispatcher.registerQueryHandler<events::foliage::GetTileFoliageInstancesQuery>(
        [](const events::foliage::GetTileFoliageInstancesQuery&) -> std::vector<foliage::FoliageInstance>
        {
            return {};
        });
    // The instanced write path — count what the brush pushes.
    dispatcher.registerCommandHandler<events::foliage::AddFoliageInstancesToTileCommand>(
        [&addedInstances](const events::foliage::AddFoliageInstancesToTileCommand& cmd)
        {
            addedInstances += static_cast<uint32_t>(cmd.instances.size());
        });
    // The entity path must NEVER fire for the instanced backend.
    dispatcher.registerCommandHandler<events::scene::CreateEntityCommand>(
        [&createEntityCount](const events::scene::CreateEntityCommand&) -> services::EntityHandle
        {
            ++createEntityCount;
            return services::EntityHandle{};
        });

    services::FoliageBrushServiceImpl service;
    service.registerEventHandlers();

    // Activate the brush and widen it so a stroke reliably places several instances.
    events::foliageBrush::FoliageBrushModeChangedNotification active;
    active.isActive = true;
    dispatcher.publish(active);

    events::foliageBrush::SetFoliageBrushParamsCommand paramsCmd;
    paramsCmd.params.radius = 10.0f;
    paramsCmd.params.spacing = 1.0f;
    paramsCmd.params.density = 1.0f;
    paramsCmd.params.placementMode = foliage::FoliagePlacementMode::Spray;
    dispatcher.execute(paramsCmd);

    events::foliageBrush::ApplyFoliageBrushCommand apply;
    apply.worldPosition = glm::vec3(0.0f);
    apply.deltaTime = 1.0f;          // flowRate*dt >> 1 => a spray fires immediately
    apply.isFirstApplication = true;
    dispatcher.execute(apply);

    CHECK(createEntityCount == 0);   // instanced backend creates NO ECS entities
    CHECK(addedInstances > 0);        // ...but it does write to the tile store

    dispatcher.clear();
}

} // TEST_SUITE("FoliageBrush")
