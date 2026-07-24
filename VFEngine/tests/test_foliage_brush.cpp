#include <doctest.h>

#include <foliage/FoliageScatter.hpp>
#include <foliage/FoliageSpatialGrid.hpp>
#include <foliage/FoliageSerializer.hpp>
#include <foliage/FoliageTypes.hpp>
#include <foliage/FoliageCompose.hpp> // VK-1582: keepFoliageAtScale / effectiveFoliageDensityScale
#include <terrain/TerrainTile.hpp>

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
#include <set>
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
    out[0].affectedByDensityScale = false; // VK-1582
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
    CHECK_FALSE(in[0].affectedByDensityScale); // VK-1582
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

// ---- VK-1582: opt-out key defaults to true for older palettes ----

TEST_CASE("FoliageSerializer: missing affectedByDensityScale defaults to true (back-compat)") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_foliage_palette_nokey.json";
    {
        std::ofstream f(tmp);
        f << R"([{"meshPath":"a.vfMesh","densityScale":0.5}])"; // no affectedByDensityScale key
    }
    std::vector<foliage::FoliageType> in;
    REQUIRE(foliage::FoliageSerializer::loadFoliagePalette(tmp.string(), in));
    REQUIRE(in.size() == 1);
    CHECK(in[0].affectedByDensityScale);                 // defaulted true
    CHECK(in[0].densityScale == doctest::Approx(0.5f));
    fs::remove(tmp);
}

// ============================================================
// VK-1582: deterministic density-scale subset selection
// ============================================================

namespace {
    // Spread of non-sequential seeds like the ones authoring bakes onto FoliageInstance.seed.
    std::vector<uint32_t> makeSeeds(uint32_t n)
    {
        std::vector<uint32_t> s;
        s.reserve(n);
        for (uint32_t i = 0; i < n; ++i) s.push_back(i * 2654435761u + 1013904223u);
        return s;
    }
}

TEST_CASE("Density scale: same seed set + scale yields an identical survivor set") {
    const auto seeds = makeSeeds(1000);
    auto survivors = [&](float scale) {
        std::vector<uint32_t> out;
        for (uint32_t s : seeds)
            if (foliage::keepFoliageAtScale(s, scale)) out.push_back(s);
        return out;
    };
    CHECK(survivors(0.37f) == survivors(0.37f)); // deterministic, no frame-to-frame churn
    CHECK(survivors(0.80f) == survivors(0.80f));
}

TEST_CASE("Density scale: survivor set is monotone (nested) as the scale rises") {
    const auto seeds = makeSeeds(4000);
    auto survivorSet = [&](float scale) {
        std::set<uint32_t> out;
        for (uint32_t s : seeds)
            if (foliage::keepFoliageAtScale(s, scale)) out.insert(s);
        return out;
    };
    const auto lo  = survivorSet(0.3f);
    const auto mid = survivorSet(0.6f);
    const auto hi  = survivorSet(1.0f);

    // Raising the scale only ever ADDS survivors: lo ⊆ mid ⊆ hi (this is what prevents popping
    // when the density knob moves — a survivor at a lower scale always survives at a higher one).
    for (uint32_t s : lo)  CHECK(mid.count(s) == 1);
    for (uint32_t s : mid) CHECK(hi.count(s) == 1);

    CHECK(lo.size() <= mid.size());
    CHECK(mid.size() <= hi.size());
    CHECK(hi.size() == seeds.size()); // scale 1.0 keeps everything
}

TEST_CASE("Density scale: bounds keep-all at >= 1 and keep-none at <= 0") {
    const auto seeds = makeSeeds(200);
    for (uint32_t s : seeds) {
        CHECK(foliage::keepFoliageAtScale(s, 1.0f));
        CHECK(foliage::keepFoliageAtScale(s, 1.5f));
        CHECK_FALSE(foliage::keepFoliageAtScale(s, 0.0f));
        CHECK_FALSE(foliage::keepFoliageAtScale(s, -0.25f));
    }
}

TEST_CASE("Density scale: effective scale = per-type densityScale x global, with opt-out") {
    foliage::FoliageType affected;                 // defaults: densityScale 1, affected = true
    foliage::FoliageType optedOut;  optedOut.affectedByDensityScale = false;
    foliage::FoliageType halfType;  halfType.densityScale = 0.5f;

    // Opt-out ignores the global scale; affected multiplies by it; per-type densityScale multiplies too.
    CHECK(foliage::effectiveFoliageDensityScale(optedOut, 0.25f) == doctest::Approx(1.0f));
    CHECK(foliage::effectiveFoliageDensityScale(affected, 0.25f) == doctest::Approx(0.25f));
    CHECK(foliage::effectiveFoliageDensityScale(halfType, 0.5f)  == doctest::Approx(0.25f));

    const auto seeds = makeSeeds(2000);
    auto survivorCount = [&](const foliage::FoliageType& t, float global) {
        size_t n = 0;
        const float eff = foliage::effectiveFoliageDensityScale(t, global);
        for (uint32_t s : seeds)
            if (foliage::keepFoliageAtScale(s, eff)) ++n;
        return n;
    };
    CHECK(survivorCount(optedOut, 0.1f) == survivorCount(optedOut, 0.9f)); // opt-out: global has no effect
    CHECK(survivorCount(affected, 0.1f) <  survivorCount(affected, 0.9f)); // affected: fewer at lower global
}

TEST_CASE("Density scale: survivor fraction approximates the effective scale") {
    const auto seeds = makeSeeds(40000);
    auto fraction = [&](float scale) {
        size_t n = 0;
        for (uint32_t s : seeds)
            if (foliage::keepFoliageAtScale(s, scale)) ++n;
        return static_cast<double>(n) / static_cast<double>(seeds.size());
    };
    CHECK(fraction(0.25f) == doctest::Approx(0.25).epsilon(0.04));
    CHECK(fraction(0.50f) == doctest::Approx(0.50).epsilon(0.04));
    CHECK(fraction(0.75f) == doctest::Approx(0.75).epsilon(0.04));
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

// ---- code-review #1: hasChanges() must detect content edits, not just count changes ----

TEST_CASE("FoliageTileSnapshotUndoCommand: hasChanges detects same-count content edits") {
    // A re-bake / edit that replaces N instances with N *different* instances (same count) must
    // still be undoable — otherwise Ctrl+Z is a silent no-op and the prior layout is lost.
    std::vector<foliage::FoliageInstance> before(4);
    for (uint32_t i = 0; i < 4; ++i)
        before[i].position = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);

    SUBCASE("identical content -> no change") {
        std::vector<foliage::FoliageInstance> after = before; // byte-identical copy
        services::FoliageTileSnapshotUndoCommand cmd("Foliage Brush");
        cmd.addTile(0, 0, before, after);
        CHECK_FALSE(cmd.hasChanges());
    }
    SUBCASE("same count, moved instance -> change") {
        std::vector<foliage::FoliageInstance> after = before;
        after[2].position.x += 5.0f; // moved one instance, count unchanged
        services::FoliageTileSnapshotUndoCommand cmd("Foliage Brush");
        cmd.addTile(0, 0, before, after);
        CHECK(cmd.hasChanges());
    }
    SUBCASE("same count, changed appearance (tint/scale) -> change") {
        std::vector<foliage::FoliageInstance> after = before;
        after[0].tint = 0x11223344u;
        after[1].scale = glm::vec3(2.0f);
        services::FoliageTileSnapshotUndoCommand cmd("Foliage Brush");
        cmd.addTile(0, 0, before, after);
        CHECK(cmd.hasChanges());
    }
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

// ============================================================
// VK-1579: foliage rides terrain-tile streaming (.vfFoliage sidecars)
// ============================================================
//
// streamInTile() itself is not CPU-unit-testable (it needs createTileEntity -> scene
// graph and addTileFromFile -> terrain file cache). These cases exercise the exact
// per-tile load/flag/free logic the VK-1579 block runs, at the tile-data level.

namespace {
    // Mirrors the foliage block added to TerrainService::streamInTile (VK-1579), which in
    // turn mirrors the vegetation restore block. The on-disk layout matches
    // TerrainService::getFoliageDirectory (TerrainDataIOOps.cpp:226-230): a "<stem>_foliage"
    // directory beside the terrain save file, holding "tile_<x>_<z>.vfFoliage" sidecars.
    // Keep this in sync with TerrainStreamingOps.cpp if that block changes.
    bool streamInFoliageForTile(const std::string& savePath, int tileX, int tileZ,
                                terrain::TerrainTile& tile)
    {
        namespace fs = std::filesystem;
        fs::path p(savePath);
        std::string foliageDir = (p.parent_path() / (p.stem().string() + "_foliage")).string();
        std::string foliagePath = foliageDir + "/tile_" + std::to_string(tileX) + "_" +
                                  std::to_string(tileZ) + ".vfFoliage";
        if (!fs::exists(foliagePath))
            return false;
        foliage::FoliageSerializer::loadFoliageInstances(foliagePath, tile.foliageInstances);
        tile.foliageInstancesDirty = true;
        tile.foliageInstancesGPUDirty = true;
        return true;
    }

    // Write a tile sidecar under the getFoliageDirectory layout, creating the dir first.
    void writeFoliageSidecar(const std::string& savePath, int tileX, int tileZ,
                             const std::vector<foliage::FoliageInstance>& instances)
    {
        namespace fs = std::filesystem;
        fs::path p(savePath);
        fs::path foliageDir = p.parent_path() / (p.stem().string() + "_foliage");
        fs::create_directories(foliageDir);
        std::string foliagePath = (foliageDir / ("tile_" + std::to_string(tileX) + "_" +
                                   std::to_string(tileZ) + ".vfFoliage")).string();
        REQUIRE(foliage::FoliageSerializer::saveFoliageInstances(foliagePath, instances));
    }
}

TEST_CASE("stream-in loads the tile's .vfFoliage sidecar and raises both dirty flags") {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "vf1579_stream_in";
    fs::remove_all(root);
    fs::create_directories(root);
    std::string savePath = (root / "world.vfWorld").string();

    std::vector<foliage::FoliageInstance> saved(3);
    saved[0].position = glm::vec3(12.0f, 3.0f, -7.0f);
    saved[0].typeIndex = 9;
    saved[0].seed = 0xABCDEF01u;
    saved[1].typeIndex = 40;
    saved[2].typeIndex = 63;
    writeFoliageSidecar(savePath, 2, 3, saved);

    // Fresh tile, as produced by grid.addTileFromFile before the foliage restore.
    terrain::TerrainTile tile;
    REQUIRE_FALSE(tile.hasFoliageInstances());
    REQUIRE_FALSE(tile.foliageInstancesDirty);
    REQUIRE_FALSE(tile.foliageInstancesGPUDirty);

    bool loaded = streamInFoliageForTile(savePath, 2, 3, tile);

    CHECK(loaded);
    CHECK(tile.hasFoliageInstances());
    CHECK(tile.foliageInstances.size() == 3u);
    CHECK(tile.foliageInstances[0].typeIndex == 9u);
    CHECK(tile.foliageInstances[0].position.x == doctest::Approx(12.0f));
    CHECK(tile.foliageInstances[0].seed == 0xABCDEF01u);
    CHECK(tile.foliageInstances[2].typeIndex == 63u);
    CHECK(tile.foliageInstancesDirty);      // save gate
    CHECK(tile.foliageInstancesGPUDirty);   // collector recompose gate

    fs::remove_all(root);
}

TEST_CASE("stream-in of a tile with no sidecar leaves it empty and unflagged") {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "vf1579_stream_in_missing";
    fs::remove_all(root);
    fs::create_directories(root);
    std::string savePath = (root / "world.vfWorld").string();

    // A sidecar exists for (2,3) but NOT for the tile we stream in (9,9).
    writeFoliageSidecar(savePath, 2, 3, std::vector<foliage::FoliageInstance>(1));

    terrain::TerrainTile tile;
    bool loaded = streamInFoliageForTile(savePath, 9, 9, tile);

    CHECK_FALSE(loaded);
    CHECK_FALSE(tile.hasFoliageInstances());
    CHECK_FALSE(tile.foliageInstancesDirty);
    CHECK_FALSE(tile.foliageInstancesGPUDirty);

    fs::remove_all(root);
}

TEST_CASE("stream-out frees a tile's foliage with the tile (no explicit unload needed)") {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "vf1579_stream_out";
    fs::remove_all(root);
    fs::create_directories(root);
    std::string savePath = (root / "world.vfWorld").string();
    writeFoliageSidecar(savePath, 5, 5, std::vector<foliage::FoliageInstance>(4));

    // Stream in on a heap tile (grid owns tiles); foliage becomes resident.
    auto tile = std::make_unique<terrain::TerrainTile>();
    REQUIRE(streamInFoliageForTile(savePath, 5, 5, *tile));
    REQUIRE(tile->foliageInstances.size() == 4u);

    // Stream out == grid.removeTile(coord): destroying the tile frees foliageInstances.
    // No per-field cleanup runs anywhere, and none is needed.
    tile.reset();

    // A tile re-streamed at the same coord starts empty and only regains foliage by
    // re-loading its own sidecar — foliage residency is bound purely to tile lifetime.
    terrain::TerrainTile restreamed;
    CHECK_FALSE(restreamed.hasFoliageInstances());
    REQUIRE(streamInFoliageForTile(savePath, 5, 5, restreamed));
    CHECK(restreamed.foliageInstances.size() == 4u);

    fs::remove_all(root);
}

} // TEST_SUITE("FoliageBrush")
