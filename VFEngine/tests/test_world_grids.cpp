#include <doctest.h>
#include <world/WorldTypes.hpp>
#include <world/WorldSector.hpp>
#include <world/WorldSectorManager.hpp>
#include <world/SectorStreamer.hpp>
#include <world/GridActionMerge.hpp>
#include <world/SectorAssignment.hpp>
#include <world/WorldDefinition.hpp>
#include <world/WorldDefinitionSerialization.hpp>
#include <streaming/BudgetedEvictionPool.hpp> // VK-1600: pool re-keying after a grid removal

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

// ============================================================
// VK-1599 - multiple named runtime streaming grids.
//
// Everything here is CPU-only: the grid model is a data structure plus two pure functions
// (sectorRegistrationId, mergeGridStreamingActions) and a file format. WorldSectorServiceImpl needs
// a SceneGraphSystem and drives EventDispatcher throughout, so it is never constructed by a test -
// the same split test_sector_assignment and test_hlod_cell_planner already rely on.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    constexpr auto kActivate = world::SectorTargetState::Activated;
    constexpr auto kUnload = world::SectorTargetState::Unloaded;

    // Mirrors test_sector_streamer.cpp's fixture so the multi-grid cases read like the single-grid
    // ones next door.
    world::SectorConfig makeSectorConfig(float sectorSize)
    {
        world::SectorConfig config;
        config.sectorWorldSize = sectorSize;
        return config;
    }

    void populateGrid(world::WorldSectorManager& manager, int extent)
    {
        for (int x = -extent; x <= extent; ++x)
        {
            for (int z = -extent; z <= extent; ++z)
            {
                auto& sector = manager.getOrCreateSector({x, z});
                sector.state = world::SectorState::Unloaded;
                sector.filePath = "sector.vfsector";
            }
        }
    }

    world::StreamingSource sourceAt(const glm::vec3& position)
    {
        world::StreamingSource source;
        source.position = position;
        return source;
    }

    int countTargets(const std::vector<world::SectorStreamingAction>& actions,
                     world::SectorTargetState target)
    {
        return static_cast<int>(std::count_if(actions.begin(), actions.end(),
            [&](const world::SectorStreamingAction& a) { return a.target == target; }));
    }

    fs::path testRoot()
    {
        return fs::temp_directory_path() / "vf_world_grid_tests";
    }

    void resetTestRoot()
    {
        std::error_code ec;
        fs::remove_all(testRoot(), ec);
        fs::create_directories(testRoot(), ec);
    }

    std::string readFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    world::EntityStreamingTraits spatialTraits(const glm::vec3& position, uint8_t gridIndex)
    {
        world::EntityStreamingTraits traits;
        traits.hasTransform = true;
        traits.position = position;
        traits.gridIndex = gridIndex;
        return traits;
    }
}

TEST_SUITE("VK-1599 world grids")
{
    // ── sectorRegistrationId ────────────────────────────────────────────

    TEST_CASE("sectorRegistrationId: grid 0 reproduces the legacy sector id exactly")
    {
        // The whole no-behaviour-change claim for a one-grid world rests on this: every GPU slot,
        // light slot and scheduler hint a single-grid world registers keeps the value it always had.
        const world::SectorCoord coords[] = {
            {0, 0}, {1, 0}, {0, 1}, {-1, -1}, {5, -3},
            {world::kMinSectorCoord, world::kMaxSectorCoord},
        };

        for (const auto& coord : coords)
        {
            CHECK(world::sectorRegistrationId(world::kPrimaryGridIndex, coord)
                  == static_cast<uint64_t>(world::sectorCoordToId(coord)));
        }
    }

    TEST_CASE("sectorRegistrationId: injective over grid x coord")
    {
        // The property the whole design turns on. Two grids WILL hold a sector at the same coord -
        // that is the point of the feature - and if their ids collided they would silently share a
        // GPU streaming slot, which is the exact failure sectorCoordToId's own comment warns about
        // one dimension down.
        //
        // Sampled rather than exhaustive: the full space is 8 x 65536^2.
        const int32_t axisSamples[] = {
            world::kMinSectorCoord, world::kMinSectorCoord + 1, -4096, -1, 0, 1, 4096,
            world::kMaxSectorCoord - 1, world::kMaxSectorCoord,
        };

        std::unordered_set<uint64_t> seen;
        size_t total = 0;

        for (uint8_t grid = 0; grid < world::kMaxGrids; ++grid)
        {
            for (int32_t x : axisSamples)
            {
                for (int32_t z : axisSamples)
                {
                    const uint64_t id = world::sectorRegistrationId(grid, {x, z});
                    CHECK(seen.insert(id).second);
                    ++total;
                }
            }
        }

        CHECK(seen.size() == total);
    }

    TEST_CASE("sectorRegistrationId: the same coord on two grids never aliases")
    {
        for (uint8_t grid = 1; grid < world::kMaxGrids; ++grid)
        {
            CHECK(world::sectorRegistrationId(0, {7, -9})
                  != world::sectorRegistrationId(grid, {7, -9}));
        }
    }

    TEST_CASE("sectorRegistrationId: the grid rides above the packed coord")
    {
        // Bits 32-39, which is exactly where hlodGuidValue already puts the HLOD tier over the same
        // base - so folding a grid into the synthetic sector AssetGUID stays collision-free against
        // the HLOD family without touching AssetGUID.
        const world::SectorCoord coord{12, -34};
        for (uint8_t grid = 0; grid < world::kMaxGrids; ++grid)
        {
            const uint64_t id = world::sectorRegistrationId(grid, coord);
            CHECK((id & 0xFFFFFFFFull) == world::sectorCoordToId(coord));
            CHECK((id >> 32) == grid);
        }
    }

    // ── sectorFileName ──────────────────────────────────────────────────

    TEST_CASE("sectorFileName: the primary grid keeps the historical spelling")
    {
        // Load-bearing: a world's existing .vfworld inventory names these files, and a rename would
        // orphan every sector it lists.
        CHECK(world::sectorFileName(world::kPrimaryGridIndex, {3, -4}) == "sector_3_-4.vfsector");
        CHECK(world::sectorFileName(world::kPrimaryGridIndex, {0, 0}) == "sector_0_0.vfsector");
    }

    TEST_CASE("sectorFileName: extra grids are namespaced and never collide with grid 0")
    {
        CHECK(world::sectorFileName(1, {3, -4}) == "sector_g1_3_-4.vfsector");
        CHECK(world::sectorFileName(2, {0, 0}) == "sector_g2_0_0.vfsector");

        std::unordered_set<std::string> names;
        for (uint8_t grid = 0; grid < world::kMaxGrids; ++grid)
            CHECK(names.insert(world::sectorFileName(grid, {5, 5})).second);
    }

    // ── two grids stream independently ──────────────────────────────────

    TEST_CASE("two grids at different cell sizes stream independently from one source")
    {
        // AC #1, at the level a CPU test can reach it: one source, two managers, two streamers,
        // each producing a ring sized by ITS OWN config.
        world::WorldSectorManager coarse(makeSectorConfig(100.0f));
        world::WorldSectorManager fine(makeSectorConfig(25.0f));
        populateGrid(coarse, 12);
        populateGrid(fine, 12);

        world::SectorStreamingConfig coarseConfig;
        coarseConfig.loadRadius = 4.0f;
        coarseConfig.unloadRadius = 6.0f;
        coarseConfig.maxLoadsPerFrame = 64;

        world::SectorStreamingConfig fineConfig;
        fineConfig.loadRadius = 1.0f;
        fineConfig.unloadRadius = 3.0f;
        fineConfig.maxLoadsPerFrame = 64;

        world::SectorStreamer coarseStreamer(coarseConfig);
        world::SectorStreamer fineStreamer(fineConfig);
        coarseStreamer.setEnabled(true);
        fineStreamer.setEnabled(true);

        const std::vector<world::StreamingSource> sources{sourceAt({50.0f, 0.0f, 50.0f})};

        std::vector<world::SectorStreamingAction> coarseActions;
        std::vector<world::SectorStreamingAction> fineActions;
        coarseStreamer.update(sources, coarse, coarseActions);
        fineStreamer.update(sources, fine, fineActions);

        const int coarseLoads = countTargets(coarseActions, kActivate);
        const int fineLoads = countTargets(fineActions, kActivate);

        CHECK(coarseLoads > 0);
        CHECK(fineLoads > 0);
        // The radii are in SECTOR counts, so a 4-sector ring covers strictly more cells than a
        // 1-sector one whatever the cell size is. If the two streamers shared a config this would
        // come out equal.
        CHECK(coarseLoads > fineLoads);
    }

    TEST_CASE("one grid's streamer state does not disturb another's")
    {
        world::WorldSectorManager a(makeSectorConfig(100.0f));
        world::WorldSectorManager b(makeSectorConfig(100.0f));
        populateGrid(a, 6);
        populateGrid(b, 6);

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.unloadRadius = 4.0f;
        config.maxLoadsPerFrame = 64;
        config.maxUnloadsPerFrame = 64;

        world::SectorStreamer streamerA(config);
        world::SectorStreamer streamerB(config);
        streamerA.setEnabled(true);
        streamerB.setEnabled(true);

        std::vector<world::SectorStreamingAction> actionsA;
        std::vector<world::SectorStreamingAction> actionsB;

        const std::vector<world::StreamingSource> origin{sourceAt({50.0f, 0.0f, 50.0f})};
        streamerA.update(origin, a, actionsA);
        streamerB.update(origin, b, actionsB);
        CHECK(actionsA.size() == actionsB.size());

        // Freeze A only. B must be entirely unaffected - the pause latch is per streamer.
        streamerA.setPaused(true);
        streamerA.update(origin, a, actionsA);
        streamerB.update(origin, b, actionsB);

        CHECK(actionsA.empty());
        CHECK(streamerA.isFrozen());
        CHECK_FALSE(streamerB.isFrozen());
    }

    // ── mergeGridStreamingActions ───────────────────────────────────────

    TEST_CASE("merge: empty input produces nothing and clears the output")
    {
        std::vector<world::GridStreamingAction> merged;
        merged.push_back({3, {{9, 9}, kActivate}}); // stale content from a previous frame

        world::mergeGridStreamingActions({}, merged);
        CHECK(merged.empty());

        world::mergeGridStreamingActions({{}, {}}, merged);
        CHECK(merged.empty());
    }

    TEST_CASE("merge: a single grid is a straight copy, tagged with its index")
    {
        std::vector<std::vector<world::SectorStreamingAction>> perGrid(1);
        perGrid[0] = {{{0, 0}, kActivate}, {{1, 0}, kActivate}, {{2, 0}, kUnload}};

        std::vector<world::GridStreamingAction> merged;
        world::mergeGridStreamingActions(perGrid, merged);

        REQUIRE(merged.size() == 3);
        for (size_t i = 0; i < merged.size(); ++i)
        {
            CHECK(merged[i].gridIndex == 0);
            CHECK(merged[i].action.coord == perGrid[0][i].coord);
            CHECK(merged[i].action.target == perGrid[0][i].target);
        }
    }

    TEST_CASE("merge: round-robin, grid 0 first in every round")
    {
        std::vector<std::vector<world::SectorStreamingAction>> perGrid(3);
        perGrid[0] = {{{0, 0}, kActivate}, {{0, 1}, kActivate}};
        perGrid[1] = {{{1, 0}, kActivate}, {{1, 1}, kActivate}};
        perGrid[2] = {{{2, 0}, kActivate}, {{2, 1}, kActivate}};

        std::vector<world::GridStreamingAction> merged;
        world::mergeGridStreamingActions(perGrid, merged);

        REQUIRE(merged.size() == 6);
        const uint8_t expectedGrids[] = {0, 1, 2, 0, 1, 2};
        for (size_t i = 0; i < merged.size(); ++i)
            CHECK(merged[i].gridIndex == expectedGrids[i]);
    }

    TEST_CASE("merge: no grid ever runs more than one action ahead of another")
    {
        // The starvation property the interleave exists for. With plain concatenation the busy
        // grid would consume the whole frame's downstream budget before the quiet one was reached.
        std::vector<std::vector<world::SectorStreamingAction>> perGrid(2);
        for (int i = 0; i < 20; ++i)
            perGrid[0].push_back({{i, 0}, kActivate});
        for (int i = 0; i < 20; ++i)
            perGrid[1].push_back({{i, 1}, kActivate});

        std::vector<world::GridStreamingAction> merged;
        world::mergeGridStreamingActions(perGrid, merged);

        REQUIRE(merged.size() == 40);
        int counts[2] = {0, 0};
        for (const auto& entry : merged)
        {
            ++counts[entry.gridIndex];
            CHECK(std::abs(counts[0] - counts[1]) <= 1);
        }
    }

    TEST_CASE("merge: a short list never holds up the rest")
    {
        std::vector<std::vector<world::SectorStreamingAction>> perGrid(2);
        perGrid[0] = {{{0, 0}, kActivate}};
        perGrid[1] = {{{1, 0}, kActivate}, {{1, 1}, kActivate}, {{1, 2}, kActivate}};

        std::vector<world::GridStreamingAction> merged;
        world::mergeGridStreamingActions(perGrid, merged);

        REQUIRE(merged.size() == 4);
        CHECK(merged[0].gridIndex == 0);
        CHECK(merged[1].gridIndex == 1);
        CHECK(merged[2].gridIndex == 1);
        CHECK(merged[3].gridIndex == 1);
    }

    TEST_CASE("merge: each grid's own ordering survives intact")
    {
        // SectorStreamer has already sorted its candidates by distance, priority and view bias.
        // Reordering within a grid would throw that away.
        std::vector<std::vector<world::SectorStreamingAction>> perGrid(2);
        perGrid[0] = {{{0, 0}, kActivate}, {{0, 1}, kActivate}, {{0, 2}, kUnload}};
        perGrid[1] = {{{1, 0}, kUnload}, {{1, 1}, kActivate}};

        std::vector<world::GridStreamingAction> merged;
        world::mergeGridStreamingActions(perGrid, merged);

        for (uint8_t grid = 0; grid < 2; ++grid)
        {
            size_t next = 0;
            for (const auto& entry : merged)
            {
                if (entry.gridIndex != grid)
                    continue;
                REQUIRE(next < perGrid[grid].size());
                CHECK(entry.action.coord == perGrid[grid][next].coord);
                CHECK(entry.action.target == perGrid[grid][next].target);
                ++next;
            }
            CHECK(next == perGrid[grid].size());
        }
    }

    // ── grid-aware entity assignment ────────────────────────────────────

    TEST_CASE("resolveSectorAssignment: the entity's grid picks the config its coord is derived from")
    {
        const world::SectorConfig configs[] = {makeSectorConfig(100.0f), makeSectorConfig(25.0f)};
        const std::span<const world::SectorConfig> span(configs, 2);

        const glm::vec3 position{150.0f, 0.0f, 150.0f};

        const auto onCoarse = world::resolveSectorAssignment(spatialTraits(position, 0), span);
        REQUIRE(onCoarse.isSpatial());
        CHECK(onCoarse.gridIndex == 0);
        CHECK(onCoarse.coord == world::SectorCoord(1, 1)); // 150 / 100

        const auto onFine = world::resolveSectorAssignment(spatialTraits(position, 1), span);
        REQUIRE(onFine.isSpatial());
        CHECK(onFine.gridIndex == 1);
        CHECK(onFine.coord == world::SectorCoord(6, 6)); // 150 / 25
    }

    TEST_CASE("resolveSectorAssignment: an out-of-range grid falls back to the primary one")
    {
        // A .vfscene can name a grid a later .vfworld edit removed. Dropping the entity out of the
        // sector system entirely would be data loss on the next Save World, so it lands on grid 0.
        const world::SectorConfig configs[] = {makeSectorConfig(100.0f)};
        const std::span<const world::SectorConfig> span(configs, 1);

        const auto result = world::resolveSectorAssignment(
            spatialTraits({150.0f, 0.0f, 150.0f}, 5), span);

        REQUIRE(result.isSpatial());
        CHECK(result.gridIndex == world::kPrimaryGridIndex);
        CHECK(result.coord == world::SectorCoord(1, 1));
    }

    TEST_CASE("resolveSectorAssignment: an empty grid list is safe")
    {
        // "No world open" must not read out of bounds; it resolves against a default grid.
        // Spelled out rather than braced: a bare {} is ambiguous between the span overload and the
        // single-config one, and the span is the one under test here.
        const std::span<const world::SectorConfig> empty;
        const auto result = world::resolveSectorAssignment(
            spatialTraits({150.0f, 0.0f, 150.0f}, 3), empty);

        REQUIRE(result.isSpatial());
        CHECK(result.gridIndex == world::kPrimaryGridIndex);
        CHECK(result.coord == world::SectorCoord(1, 1)); // the 128-unit struct default
    }

    TEST_CASE("resolveSectorAssignment: the refusals still win over the grid")
    {
        const world::SectorConfig configs[] = {makeSectorConfig(100.0f), makeSectorConfig(25.0f)};
        const std::span<const world::SectorConfig> span(configs, 2);

        auto pinned = spatialTraits({10.0f, 0.0f, 10.0f}, 1);
        pinned.spatiallyLoaded = false;
        CHECK(world::resolveSectorAssignment(pinned, span).kind
              == world::SectorAssignmentKind::NotSpatiallyLoaded);

        auto managed = spatialTraits({10.0f, 0.0f, 10.0f}, 1);
        managed.managedBySeparateSystem = true;
        CHECK(world::resolveSectorAssignment(managed, span).kind
              == world::SectorAssignmentKind::ManagedBySeparateSystem);

        world::EntityStreamingTraits noTransform;
        noTransform.gridIndex = 1;
        CHECK(world::resolveSectorAssignment(noTransform, span).kind
              == world::SectorAssignmentKind::NoTransform);
    }

    TEST_CASE("resolveSectorAssignment: the single-config overload always resolves on grid 0")
    {
        // The shape every pre-VK-1599 caller used, and the one VK-1598's cold planner still wants:
        // it works on ONE grid's sector files at a time and hands in that grid's config.
        const auto result = world::resolveSectorAssignment(
            spatialTraits({150.0f, 0.0f, 150.0f}, 3), makeSectorConfig(100.0f));

        REQUIRE(result.isSpatial());
        CHECK(result.gridIndex == world::kPrimaryGridIndex);
        CHECK(result.coord == world::SectorCoord(1, 1));
    }

    // ── .vfworld format ─────────────────────────────────────────────────

    TEST_CASE(".vfworld: a one-grid world writes no grids key at all")
    {
        // The byte-identity guarantee. A world that never touches grids must produce exactly the
        // file a pre-VK-1599 build produced, or every existing project churns on its next save.
        resetTestRoot();

        world::WorldDefinition definition;
        definition.name = "SingleGrid";
        definition.primaryGrid().sectorConfig.sectorWorldSize = 256.0f;
        definition.primaryGrid().streamingConfig.loadRadius = 6.0f;
        definition.primaryGrid().sectorFilePaths[{1, -2}] = "sectors/sector_1_-2.vfsector";

        const std::string path = (testRoot() / "single.vfworld").string();
        REQUIRE(world::WorldDefinitionSerialization::save(definition, path));

        const std::string contents = readFile(path);
        CHECK(contents.find("\"grids\"") == std::string::npos);
        CHECK(contents.find("\"sectorConfig\"") != std::string::npos);
        CHECK(contents.find("\"streamingConfig\"") != std::string::npos);
    }

    TEST_CASE(".vfworld: a renamed primary grid does write the grids key")
    {
        resetTestRoot();

        world::WorldDefinition definition;
        definition.name = "RenamedPrimary";
        definition.primaryGrid().name = "Landmarks";

        const std::string path = (testRoot() / "renamed.vfworld").string();
        REQUIRE(world::WorldDefinitionSerialization::save(definition, path));
        CHECK(readFile(path).find("\"grids\"") != std::string::npos);

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));
        REQUIRE(loaded.grids.size() == 1);
        CHECK(loaded.primaryGrid().name == "Landmarks");
    }

    TEST_CASE(".vfworld: a world with no grids key loads as one Default grid")
    {
        // The legacy fallback: every .vfworld written before VK-1599 looks exactly like this.
        resetTestRoot();

        const std::string path = (testRoot() / "legacy.vfworld").string();
        {
            std::ofstream out(path);
            out << R"({
  "version": "1.0",
  "name": "Legacy",
  "sectorConfig": { "sectorWorldSize": 512.0, "tilesPerSector": 16, "alignedToTerrain": true },
  "streamingConfig": { "loadRadius": 7.0, "unloadRadius": 11.0 },
  "sectors": [ { "x": 2, "z": -3, "path": "sectors/sector_2_-3.vfsector" } ]
})";
        }

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));

        REQUIRE(loaded.grids.size() == 1);
        CHECK(loaded.primaryGrid().name == world::kDefaultGridName);
        CHECK(loaded.primaryGrid().sectorConfig.sectorWorldSize == doctest::Approx(512.0f));
        CHECK(loaded.primaryGrid().sectorConfig.tilesPerSector == 16);
        CHECK(loaded.primaryGrid().sectorConfig.alignedToTerrain);
        CHECK(loaded.primaryGrid().streamingConfig.loadRadius == doctest::Approx(7.0f));
        CHECK(loaded.primaryGrid().streamingConfig.unloadRadius == doctest::Approx(11.0f));
        REQUIRE(loaded.primaryGrid().sectorFilePaths.size() == 1);
        CHECK(loaded.primaryGrid().sectorFilePaths.at({2, -3}) == "sectors/sector_2_-3.vfsector");
    }

    TEST_CASE(".vfworld: a multi-grid world round-trips every grid")
    {
        resetTestRoot();

        world::WorldDefinition definition;
        definition.name = "TwoGrids";
        definition.primaryGrid().sectorConfig.sectorWorldSize = 256.0f;
        definition.primaryGrid().streamingConfig.loadRadius = 4.0f;
        definition.primaryGrid().sectorFilePaths[{0, 0}] = "sectors/sector_0_0.vfsector";

        world::GridDefinition clutter;
        clutter.name = "Clutter";
        clutter.sectorConfig.sectorWorldSize = 64.0f;
        clutter.sectorConfig.tilesPerSector = 2;
        clutter.streamingConfig.loadRadius = 2.0f;
        clutter.streamingConfig.unloadRadius = 3.0f;
        clutter.sectorFilePaths[{4, -1}] = "sectors/sector_g1_4_-1.vfsector";
        definition.grids.push_back(clutter);

        const std::string path = (testRoot() / "two.vfworld").string();
        REQUIRE(world::WorldDefinitionSerialization::save(definition, path));

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));

        REQUIRE(loaded.grids.size() == 2);

        CHECK(loaded.grid(0).name == world::kDefaultGridName);
        CHECK(loaded.grid(0).sectorConfig.sectorWorldSize == doctest::Approx(256.0f));
        CHECK(loaded.grid(0).streamingConfig.loadRadius == doctest::Approx(4.0f));
        REQUIRE(loaded.grid(0).sectorFilePaths.size() == 1);
        CHECK(loaded.grid(0).sectorFilePaths.at({0, 0}) == "sectors/sector_0_0.vfsector");

        CHECK(loaded.grid(1).name == "Clutter");
        CHECK(loaded.grid(1).sectorConfig.sectorWorldSize == doctest::Approx(64.0f));
        CHECK(loaded.grid(1).sectorConfig.tilesPerSector == 2);
        CHECK(loaded.grid(1).streamingConfig.loadRadius == doctest::Approx(2.0f));
        CHECK(loaded.grid(1).streamingConfig.unloadRadius == doctest::Approx(3.0f));
        REQUIRE(loaded.grid(1).sectorFilePaths.size() == 1);
        CHECK(loaded.grid(1).sectorFilePaths.at({4, -1}) == "sectors/sector_g1_4_-1.vfsector");
    }

    TEST_CASE(".vfworld: the primary grid is never duplicated inside the grids array")
    {
        // Two sources of truth for the same config is the bug class VK-1595 hit one level up; the
        // primary grid's config lives in the top-level keys and nowhere else.
        resetTestRoot();

        world::WorldDefinition definition;
        definition.primaryGrid().sectorConfig.sectorWorldSize = 333.0f;
        definition.grids.push_back(world::GridDefinition{});

        const std::string path = (testRoot() / "nodup.vfworld").string();
        REQUIRE(world::WorldDefinitionSerialization::save(definition, path));

        const std::string contents = readFile(path);
        // 333 appears exactly once - in the top-level sectorConfig.
        size_t occurrences = 0;
        for (size_t pos = contents.find("333."); pos != std::string::npos;
             pos = contents.find("333.", pos + 1))
        {
            ++occurrences;
        }
        CHECK(occurrences == 1);
    }

    TEST_CASE(".vfworld: a grid index outside the cap is refused rather than resized into")
    {
        // A hand-edited file must not be able to make the engine allocate 4 billion grids.
        resetTestRoot();

        const std::string path = (testRoot() / "badindex.vfworld").string();
        {
            std::ofstream out(path);
            out << R"({
  "version": "1.0",
  "name": "BadIndex",
  "sectorConfig": { "sectorWorldSize": 128.0 },
  "grids": [ { "index": 0, "name": "Default" }, { "index": 900, "name": "Nope" } ]
})";
        }

        world::WorldDefinition loaded;
        REQUIRE(world::WorldDefinitionSerialization::load(path, loaded));
        CHECK(loaded.grids.size() == 1);
    }

    // ── WorldDefinition accessors ───────────────────────────────────────

    TEST_CASE("WorldDefinition: a default-constructed world has exactly one grid")
    {
        const world::WorldDefinition definition;
        REQUIRE(definition.grids.size() == 1);
        CHECK(definition.gridCount() == 1);
        CHECK(definition.primaryGrid().name == world::kDefaultGridName);
    }

    TEST_CASE("WorldDefinition: grid() clamps rather than indexing out of bounds")
    {
        world::WorldDefinition definition;
        definition.primaryGrid().sectorConfig.sectorWorldSize = 77.0f;

        CHECK(definition.clampGridIndex(0) == 0);
        CHECK(definition.clampGridIndex(1) == 0);
        CHECK(definition.clampGridIndex(world::kMaxGrids) == 0);
        CHECK(definition.grid(9).sectorConfig.sectorWorldSize == doctest::Approx(77.0f));
    }

    // ── VK-1600: registration-id inverses ───────────────────────────────
    //
    // The eviction pools key on sectorRegistrationId so ONE flat pool can span every grid, then
    // invert it to reach the (grid, coord) release paths. A lossy inverse would evict the wrong
    // sector, so the round trip is pinned over the whole validated range and every grid.

    TEST_CASE("VK-1600: registration id round-trips over the full valid coord range")
    {
        const int32_t coords[] = {world::kMinSectorCoord, -32767, -4096, -1, 0, 1,
                                  4096, 32766, world::kMaxSectorCoord};

        for (uint8_t grid = 0; grid < world::kMaxGrids; ++grid)
        {
            for (int32_t x : coords)
            {
                for (int32_t z : coords)
                {
                    const world::SectorCoord coord(x, z);
                    REQUIRE(world::isValidSectorCoord(coord));

                    const uint64_t id = world::sectorRegistrationId(grid, coord);
                    CHECK(world::gridIndexFromRegistrationId(id) == grid);
                    CHECK(world::sectorCoordFromRegistrationId(id) == coord);
                }
            }
        }
    }

    TEST_CASE("VK-1600: the inverse is constexpr and agrees at the range corners")
    {
        // constexpr matters because it is what proves the inverse is pure arithmetic on the
        // packed bits rather than anything that could consult runtime state.
        static_assert(world::gridIndexFromRegistrationId(
                          world::sectorRegistrationId(5, world::SectorCoord(-32768, 32767))) == 5);
        static_assert(world::sectorCoordFromRegistrationId(
                          world::sectorRegistrationId(5, world::SectorCoord(-32768, 32767))).x == -32768);
        static_assert(world::sectorCoordFromRegistrationId(
                          world::sectorRegistrationId(5, world::SectorCoord(-32768, 32767))).z == 32767);

        // Grid 0 still yields the legacy value, so the inverse cannot have changed the forward
        // mapping to make itself easier.
        const world::SectorCoord coord(11, -13);
        CHECK(world::sectorRegistrationId(0, coord) == world::sectorCoordToId(coord));
    }

    TEST_CASE("VK-1600: re-keying a pool after a grid removal must run in ascending key order")
    {
        // removeGrid shifts every higher grid's index down by one. The eviction pools bake the
        // grid into their key, so their entries have to be rewritten to match - and the rewrite
        // order matters, because two grids can hold the SAME coord. Ascending key order is
        // ascending grid order, so each entry moves down into the slot the grid below it has
        // already vacated. This mirrors WorldSectorServiceImpl::rekeySectorPoolsAfterGridRemoval.
        const world::SectorCoord same(5, 5);

        auto build = []
        {
            streaming::BudgetedEvictionPool<uint64_t> pool;
            pool.put(world::sectorRegistrationId(0, world::SectorCoord(5, 5)), 10, 1, -1.0f, false);
            pool.put(world::sectorRegistrationId(2, world::SectorCoord(5, 5)), 20, 2, -2.0f, false);
            pool.put(world::sectorRegistrationId(3, world::SectorCoord(5, 5)), 30, 3, -3.0f, true);
            pool.put(world::sectorRegistrationId(3, world::SectorCoord(9, 9)), 40, 4, -4.0f, false);
            return pool;
        };

        auto rekey = [](streaming::BudgetedEvictionPool<uint64_t>& pool, uint8_t removedGrid,
                        bool ascending)
        {
            std::vector<uint64_t> keys;
            pool.forEach([&keys](uint64_t key, const auto&) { keys.push_back(key); });
            std::sort(keys.begin(), keys.end());
            if (!ascending)
                std::reverse(keys.begin(), keys.end());

            for (const uint64_t key : keys)
            {
                const uint8_t grid = world::gridIndexFromRegistrationId(key);
                if (grid < removedGrid)
                    continue;
                if (grid == removedGrid)
                {
                    pool.remove(key);
                    continue;
                }
                const auto* entry = pool.find(key);
                if (!entry)
                    continue;
                const auto moved = *entry;
                pool.remove(key);
                pool.put(world::sectorRegistrationId(static_cast<uint8_t>(grid - 1),
                                                     world::sectorCoordFromRegistrationId(key)),
                         moved.cost, moved.lastUsedFrame, moved.priority, moved.pinned);
            }
        };

        SUBCASE("ascending preserves every entry, its cost and its pin") {
            auto pool = build();
            REQUIRE(pool.size() == 4);
            REQUIRE(pool.residentCost() == 100);

            rekey(pool, 1, /*ascending=*/true);

            CHECK(pool.size() == 4);
            CHECK(pool.residentCost() == 100);

            const auto* g0 = pool.find(world::sectorRegistrationId(0, same));
            const auto* g1 = pool.find(world::sectorRegistrationId(1, same));
            const auto* g2 = pool.find(world::sectorRegistrationId(2, same));
            const auto* g2b = pool.find(world::sectorRegistrationId(2, world::SectorCoord(9, 9)));

            REQUIRE(g0 != nullptr);
            REQUIRE(g1 != nullptr);
            REQUIRE(g2 != nullptr);
            REQUIRE(g2b != nullptr);
            CHECK(g0->cost == 10);  // below the removal point: untouched
            CHECK(g1->cost == 20);  // was grid 2
            CHECK(g2->cost == 30);  // was grid 3
            CHECK(g2->pinned);      // the pin survives the move
            CHECK(g2b->cost == 40);
        }

        SUBCASE("descending silently loses the colliding entry") {
            // Pins WHY the sort is there rather than just that it is. Without ascending order the
            // grid-3 entry is re-keyed onto the grid-2 entry at the same coord before that one has
            // moved, and put() overwrites it.
            auto pool = build();
            rekey(pool, 1, /*ascending=*/false);

            CHECK(pool.size() == 3);
            CHECK(pool.residentCost() == 80);
        }
    }
}
