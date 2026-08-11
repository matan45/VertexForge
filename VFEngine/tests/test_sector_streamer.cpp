#include <doctest.h>
#include <world/WorldTypes.hpp>
#include <world/WorldSector.hpp>
#include <world/WorldSectorManager.hpp>
#include <world/SectorStreamer.hpp>

#include <algorithm>
#include <vector>

// ============================================================
// SectorStreamer decision logic (load/unload candidates,
// hysteresis, budgets, multi-source merge, seeding)
// ============================================================

namespace
{
    constexpr float kSectorSize = 100.0f;

    // VK-1591: SectorStreamingAction carries a target state instead of a load bool
    constexpr auto kActivate = world::SectorTargetState::Activated;
    constexpr auto kPrefetch = world::SectorTargetState::Prefetched;
    constexpr auto kUnload = world::SectorTargetState::Unloaded;

    world::SectorConfig makeSectorConfig()
    {
        world::SectorConfig config;
        config.sectorWorldSize = kSectorSize;
        return config;
    }

    // Populate a square grid of sectors [-extent..extent]^2, all Unloaded with a file path
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

    // Streaming source positioned at the center of sector (sx, sz)
    world::StreamingSource sourceAtSectorCenter(int sx, int sz, float radiusMultiplier = 1.0f)
    {
        world::StreamingSource source;
        source.position = glm::vec3(
            (static_cast<float>(sx) + 0.5f) * kSectorSize,
            0.0f,
            (static_cast<float>(sz) + 0.5f) * kSectorSize);
        source.radiusMultiplier = radiusMultiplier;
        return source;
    }

    int countActions(const std::vector<world::SectorStreamingAction>& actions,
                     const world::SectorCoord& coord, world::SectorTargetState target)
    {
        return static_cast<int>(std::count_if(actions.begin(), actions.end(),
            [&](const world::SectorStreamingAction& a)
            { return a.coord == coord && a.target == target; }));
    }

    bool hasAction(const std::vector<world::SectorStreamingAction>& actions,
                   const world::SectorCoord& coord, world::SectorTargetState target)
    {
        return countActions(actions, coord, target) > 0;
    }

    int countTargets(const std::vector<world::SectorStreamingAction>& actions,
                     world::SectorTargetState target)
    {
        return static_cast<int>(std::count_if(actions.begin(), actions.end(),
            [&](const world::SectorStreamingAction& a) { return a.target == target; }));
    }
}

TEST_SUITE("SectorStreamer")
{
    TEST_CASE("disabled streamer emits no actions")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 2);

        world::SectorStreamer streamer;
        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        CHECK(actions.empty());
    }

    TEST_CASE("no sources emits no actions")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 2);

        world::SectorStreamer streamer;
        streamer.setEnabled(true);
        std::vector<world::StreamingSource> sources;
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        CHECK(actions.empty());
    }

    TEST_CASE("loads nearest sector first, one per frame")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 3);

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.maxLoadsPerFrame = 1;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true);

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        REQUIRE(actions.size() == 1);
        CHECK(actions[0].target == kActivate);
        CHECK(actions[0].coord == world::SectorCoord(0, 0)); // distance 0 from source

        // Simulate the service marking the sector as loading; next frame picks an
        // axis-aligned neighbor (all at distance == one sector size, nearest remaining)
        manager.getSector(actions[0].coord)->state = world::SectorState::Loading;

        actions.clear();
        streamer.update(sources, manager, actions);
        REQUIRE(actions.size() == 1);
        CHECK(actions[0].target == kActivate);
        const auto& c = actions[0].coord;
        bool isAxisNeighbor =
            (std::abs(c.x) + std::abs(c.z)) == 1;
        CHECK(isAxisNeighbor);
    }

    TEST_CASE("maxLoadsPerFrame caps the number of load actions")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 3);

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.maxLoadsPerFrame = 3;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true);

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        CHECK(actions.size() == 3);
        for (const auto& action : actions)
            CHECK(action.target == kActivate);
    }

    TEST_CASE("sectors without a file path never load")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        auto& sector = manager.getOrCreateSector({0, 0});
        sector.state = world::SectorState::Unloaded;
        sector.filePath.clear();

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.maxLoadsPerFrame = 8;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true);

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        CHECK(actions.empty());
    }

    TEST_CASE("hysteresis: loaded sector between load and unload radius is kept")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 6);

        // Sector (3,0) center is 3 sectors from the source: beyond loadRadius (2),
        // inside unloadRadius (4) -> neither loaded nor unloaded
        auto& sector = *manager.getSector({3, 0});
        sector.state = world::SectorState::Loaded;

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.unloadRadius = 4.0f;
        config.maxLoadsPerFrame = 0; // isolate unload behavior
        config.maxUnloadsPerFrame = 8;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true); // seeds (3,0) as loaded

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        CHECK_FALSE(hasAction(actions, {3, 0}, kUnload));
        CHECK_FALSE(hasAction(actions, {3, 0}, kActivate));
    }

    TEST_CASE("unloads farthest sector first under budget")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 7);
        manager.getSector({5, 0})->state = world::SectorState::Loaded;
        manager.getSector({6, 0})->state = world::SectorState::Loaded;

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.unloadRadius = 4.0f;
        config.maxLoadsPerFrame = 0;
        config.maxUnloadsPerFrame = 1;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true);

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        REQUIRE(actions.size() == 1);
        CHECK(actions[0].target == kUnload);
        CHECK(actions[0].coord == world::SectorCoord(6, 0)); // farthest goes first

        actions.clear();
        streamer.update(sources, manager, actions);
        REQUIRE(actions.size() == 1);
        CHECK(actions[0].coord == world::SectorCoord(5, 0));
    }

    TEST_CASE("dirty sectors are never auto-unloaded")
    {
        // Pins current behavior: the streamer refuses to unload dirty sectors.
        // Phase 1 (dirty-tracking rework) must consciously revisit this contract.
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 7);
        auto& sector = *manager.getSector({6, 0});
        sector.state = world::SectorState::Loaded;
        sector.dirty = true;

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.unloadRadius = 4.0f;
        config.maxLoadsPerFrame = 0;
        config.maxUnloadsPerFrame = 8;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true);

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        CHECK_FALSE(hasAction(actions, {6, 0}, kUnload));
    }

    TEST_CASE("multiple sources covering the same sector emit a single load")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 3);

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.maxLoadsPerFrame = 16;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true);

        // Both sources sit in sector (0,0)
        std::vector<world::StreamingSource> sources{
            sourceAtSectorCenter(0, 0),
            sourceAtSectorCenter(0, 0)};
        sources[1].position.x += 10.0f;

        std::vector<world::SectorStreamingAction> actions;
        streamer.update(sources, manager, actions);

        CHECK(countActions(actions, {0, 0}, kActivate) == 1);
    }

    TEST_CASE("radiusMultiplier scales a source's load reach")
    {
        world::SectorStreamingConfig config;
        config.loadRadius = 0.8f; // 80 world units: excludes axis neighbors (distance 100)
        config.maxLoadsPerFrame = 32;

        std::vector<world::SectorStreamingAction> actions;

        SUBCASE("multiplier 1: only the source's own sector")
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 3);
            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0, 1.0f)};

            streamer.update(sources, manager, actions);
            CHECK(actions.size() == 1);
            CHECK(hasAction(actions, {0, 0}, kActivate));
        }

        SUBCASE("multiplier 2: neighbors and diagonals join the ring")
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 3);
            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0, 2.0f)};

            // 160 world units: own sector (0) + 4 axis neighbors (100) + 4 diagonals (~141)
            streamer.update(sources, manager, actions);
            CHECK(actions.size() == 9);
        }
    }

    TEST_CASE("sector near any source is protected from unload")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 12);
        manager.getSector({10, 0})->state = world::SectorState::Loaded;

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.unloadRadius = 4.0f;
        config.maxLoadsPerFrame = 0;
        config.maxUnloadsPerFrame = 8;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true);

        // Source A far from (10,0); source B right on it
        std::vector<world::StreamingSource> sources{
            sourceAtSectorCenter(0, 0),
            sourceAtSectorCenter(10, 0)};

        std::vector<world::SectorStreamingAction> actions;
        streamer.update(sources, manager, actions);
        CHECK_FALSE(hasAction(actions, {10, 0}, kUnload));

        // Remove source B: now outside ALL unload radii -> unloads
        sources.pop_back();
        actions.clear();
        streamer.update(sources, manager, actions);
        CHECK(hasAction(actions, {10, 0}, kUnload));
    }

    TEST_CASE("enable seeds already-loaded sectors as unload candidates")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 12);
        manager.getSector({10, 10})->state = world::SectorState::Loaded;

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.unloadRadius = 3.0f;
        config.maxLoadsPerFrame = 0;
        config.maxUnloadsPerFrame = 8;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true); // must pick up (10,10) from manager state

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        CHECK(hasAction(actions, {10, 10}, kUnload));
    }

    TEST_CASE("sectors still Loading are not unload candidates")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 12);
        manager.getSector({10, 10})->state = world::SectorState::Loading;

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.unloadRadius = 3.0f;
        config.maxLoadsPerFrame = 0;
        config.maxUnloadsPerFrame = 8;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true);

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        CHECK_FALSE(hasAction(actions, {10, 10}, kUnload));
    }

    TEST_CASE("high-priority source wins the per-frame load budget")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 3);
        for (int x = 8; x <= 12; ++x)
        {
            for (int z = 8; z <= 12; ++z)
            {
                auto& sector = manager.getOrCreateSector({x, z});
                sector.state = world::SectorState::Unloaded;
                sector.filePath = "sector.vfsector";
            }
        }

        // Source A's best candidates: axis neighbors of (0,0), distance 100.
        // Source B sits on a loaded plateau; its best candidates are the
        // diagonals of (10,10) at distance ~141 — farther than A's.
        manager.getSector({0, 0})->state = world::SectorState::Loaded;
        manager.getSector({10, 10})->state = world::SectorState::Loaded;
        manager.getSector({9, 10})->state = world::SectorState::Loaded;
        manager.getSector({11, 10})->state = world::SectorState::Loaded;
        manager.getSector({10, 9})->state = world::SectorState::Loaded;
        manager.getSector({10, 11})->state = world::SectorState::Loaded;

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.unloadRadius = 3.0f;
        config.maxLoadsPerFrame = 1;
        config.maxUnloadsPerFrame = 0;

        auto isDiagonalOfB = [](const world::SectorCoord& c)
        {
            return std::abs(c.x - 10) == 1 && std::abs(c.z - 10) == 1;
        };
        auto isNeighborOfA = [](const world::SectorCoord& c)
        {
            return std::abs(c.x) + std::abs(c.z) == 1;
        };

        SUBCASE("equal priority: plain nearest-first, source A's candidate wins")
        {
            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{
                sourceAtSectorCenter(0, 0),
                sourceAtSectorCenter(10, 10)};

            std::vector<world::SectorStreamingAction> actions;
            streamer.update(sources, manager, actions);
            REQUIRE(actions.size() == 1);
            CHECK(actions[0].target == kActivate);
            CHECK(isNeighborOfA(actions[0].coord));
        }

        SUBCASE("priority 3 source claims the budget despite larger distance")
        {
            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{
                sourceAtSectorCenter(0, 0),
                sourceAtSectorCenter(10, 10)};
            sources[1].priority = 3; // 20000 / (1+3) = 5000 < A's 10000

            std::vector<world::SectorStreamingAction> actions;
            streamer.update(sources, manager, actions);
            REQUIRE(actions.size() == 1);
            CHECK(actions[0].target == kActivate);
            CHECK(isDiagonalOfB(actions[0].coord));
        }
    }

    // ---- VK-1589: the two behaviours gameplay-registered sources actually rely on ----

    TEST_CASE("VK-1589: a priority-1 gameplay source out-bids the priority-0 camera")
    {
        // The camera is always synthesised as priority 0 (WorldSectorStreamingOps), and the
        // default budget is ONE sector per frame. This pins down the consequence: a gameplay
        // source only has to be priority 1 to take that slot off the player's own view, even
        // when its best candidate is strictly farther away. That is why persistent AI-base
        // sources are registered at priority 0 and only the transient minimap pre-warm goes
        // above it.
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 3);
        for (int x = 8; x <= 12; ++x)
            for (int z = 8; z <= 12; ++z)
            {
                auto& sector = manager.getOrCreateSector({x, z});
                sector.state = world::SectorState::Unloaded;
                sector.filePath = "sector.vfsector";
            }

        // Camera on the centre of (0,0), which is already resident: its cheapest candidates
        // are the four axis neighbours at distance 100 -> distSq 10000.
        manager.getSector({0, 0})->state = world::SectorState::Loaded;

        // Base plateau: (10,10) and its four axis neighbours resident, so only diagonals remain.
        const world::SectorCoord plateau[] = {{10, 10}, {9, 10}, {11, 10}, {10, 9}, {10, 11}};
        for (const world::SectorCoord& c : plateau)
            manager.getSector(c)->state = world::SectorState::Loaded;

        // Deliberately OFF-centre in z. Centred, the nearest diagonal would sit at distSq
        // 20000 and priority 1 would halve it to exactly the camera's 10000 - a tie, whose
        // winner std::sort does not define. Half a sector north puts the nearest diagonals
        // ((11,9) and (9,9)) at distSq 12500 instead, so both subcases are decided, not tied.
        world::StreamingSource base;
        base.position = glm::vec3(10.5f * kSectorSize, 0.0f, 10.0f * kSectorSize);

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.unloadRadius = 3.0f;
        config.maxLoadsPerFrame = 1;
        config.maxUnloadsPerFrame = 0;

        auto isAxisNeighbourOfCamera = [](const world::SectorCoord& c)
        { return std::abs(c.x) + std::abs(c.z) == 1; };
        auto isDiagonalOfBase = [](const world::SectorCoord& c)
        { return std::abs(c.x - 10) == 1 && std::abs(c.z - 10) == 1; };

        SUBCASE("priority 0 (camera parity): the strictly nearer camera candidate wins")
        {
            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0), base};

            std::vector<world::SectorStreamingAction> actions;
            streamer.update(sources, manager, actions);
            REQUIRE(actions.size() == 1);
            CHECK(actions[0].target == kActivate);
            CHECK(isAxisNeighbourOfCamera(actions[0].coord)); // 10000 < 12500
        }

        SUBCASE("priority 1 is already enough to take the slot from the camera")
        {
            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0), base};
            sources[1].priority = 1; // 12500 / (1+1) = 6250 < the camera's 10000

            std::vector<world::SectorStreamingAction> actions;
            streamer.update(sources, manager, actions);
            REQUIRE(actions.size() == 1);
            CHECK(actions[0].target == kActivate);
            CHECK(isDiagonalOfBase(actions[0].coord));
        }
    }

    TEST_CASE("VK-1589: a radiusMultiplier 0.5 source pins its own sector, nothing wider")
    {
        // The AI-base shape: a tight source that keeps the base's own sector resident while
        // the camera is nowhere near it, without dragging in a ring of neighbours. Relies on
        // radiusMultiplier scaling the UNLOAD radius too - a load-only multiplier would let
        // the camera's pass evict the very sector the source just pulled in.
        world::SectorStreamingConfig config;
        config.loadRadius = 1.5f;   // camera reach 150; base reach 1.5 * 100 * 0.5 = 75
        config.unloadRadius = 2.5f; // camera reach 250; base reach 2.5 * 100 * 0.5 = 125
        config.maxLoadsPerFrame = 32;
        config.maxUnloadsPerFrame = 32;

        world::StreamingSource cameraSrc = sourceAtSectorCenter(0, 0);
        world::StreamingSource baseSrc = sourceAtSectorCenter(5, 5, 0.5f);

        SUBCASE("it loads its own sector and no neighbour")
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 8);
            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{cameraSrc, baseSrc};

            std::vector<world::SectorStreamingAction> actions;
            streamer.update(sources, manager, actions);

            CHECK(hasAction(actions, {5, 5}, kActivate));
            // 75 < 100, so the axis neighbours stay out of reach
            CHECK_FALSE(hasAction(actions, {4, 5}, kActivate));
            CHECK_FALSE(hasAction(actions, {6, 5}, kActivate));
            CHECK_FALSE(hasAction(actions, {5, 4}, kActivate));
            CHECK_FALSE(hasAction(actions, {5, 6}, kActivate));
        }

        SUBCASE("it protects that sector from the distant camera's unload pass")
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 8);
            manager.getSector({5, 5})->state = world::SectorState::Loaded;

            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{cameraSrc, baseSrc};

            std::vector<world::SectorStreamingAction> actions;
            streamer.update(sources, manager, actions);
            CHECK_FALSE(hasAction(actions, {5, 5}, kUnload));

            // Drop the base source (its owner died): the camera alone evicts it again.
            sources.pop_back();
            actions.clear();
            streamer.update(sources, manager, actions);
            CHECK(hasAction(actions, {5, 5}, kUnload));
        }
    }

    TEST_CASE("setConfig enforces unloadRadius > loadRadius")
    {
        world::SectorStreamer streamer;
        world::SectorStreamingConfig config;
        config.loadRadius = 5.0f;
        config.unloadRadius = 3.0f; // invalid: would oscillate

        streamer.setConfig(config);
        CHECK(streamer.getConfig().unloadRadius > streamer.getConfig().loadRadius);

        SUBCASE("VK-1591: the constructor normalizes too — it used to bypass the clamp entirely")
        {
            world::SectorStreamingConfig ctorConfig;
            ctorConfig.loadRadius = 5.0f;
            ctorConfig.unloadRadius = 3.0f;

            world::SectorStreamer ctorStreamer(ctorConfig);
            CHECK(ctorStreamer.getConfig().unloadRadius > ctorStreamer.getConfig().loadRadius);
        }
    }

    // ============================================================
    // VK-1591 — prefetch ring (Loaded-vs-Activated target states)
    // ============================================================

    TEST_CASE("VK-1591: three rings — activate inside loadRadius, prefetch between, nothing beyond")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 4);

        // kSectorSize 100: axis neighbour distance 100, diagonal ~141, (2,0) 200, (2,2) ~283
        world::SectorStreamingConfig config;
        config.loadRadius = 1.5f;     // 150 -> (0,0), axis neighbours and diagonals
        config.prefetchRadius = 2.5f; // 250 -> also (2,0) and friends
        config.unloadRadius = 3.5f;
        config.maxLoadsPerFrame = 32;
        config.maxPrefetchesPerFrame = 32;
        config.maxUnloadsPerFrame = 32;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true);

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);

        // Activate ring
        CHECK(hasAction(actions, {0, 0}, kActivate));
        CHECK(hasAction(actions, {1, 0}, kActivate));
        CHECK(hasAction(actions, {0, -1}, kActivate));
        CHECK(hasAction(actions, {1, 1}, kActivate)); // diagonal, ~141 < 150

        // Prefetch band
        CHECK(hasAction(actions, {2, 0}, kPrefetch));
        CHECK(hasAction(actions, {0, 2}, kPrefetch));
        CHECK_FALSE(hasAction(actions, {2, 0}, kActivate));

        // Beyond the prefetch ring
        CHECK_FALSE(hasAction(actions, {2, 2}, kActivate)); // ~283 > 250
        CHECK_FALSE(hasAction(actions, {2, 2}, kPrefetch));

        // No coord may receive two actions in one frame
        for (const auto& action : actions)
        {
            int total = countActions(actions, action.coord, kActivate) +
                        countActions(actions, action.coord, kPrefetch) +
                        countActions(actions, action.coord, kUnload);
            CHECK(total == 1);
        }
    }

    TEST_CASE("VK-1591: a Prefetched sector inside loadRadius is activated, not re-prefetched")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 4);

        world::SectorStreamingConfig config;
        config.loadRadius = 1.5f;
        config.prefetchRadius = 3.0f;
        config.unloadRadius = 4.0f;
        config.maxLoadsPerFrame = 8;
        config.maxPrefetchesPerFrame = 0; // isolate: only activations may be emitted
        config.maxUnloadsPerFrame = 0;

        SUBCASE("state Prefetched")
        {
            manager.getSector({1, 0})->state = world::SectorState::Prefetched;

            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
            std::vector<world::SectorStreamingAction> actions;

            streamer.update(sources, manager, actions);
            CHECK(hasAction(actions, {1, 0}, kActivate));
            CHECK_FALSE(hasAction(actions, {1, 0}, kPrefetch));
        }

        SUBCASE("state Prefetching — the in-flight read is promoted, not restarted")
        {
            manager.getSector({1, 0})->state = world::SectorState::Prefetching;

            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
            std::vector<world::SectorStreamingAction> actions;

            streamer.update(sources, manager, actions);
            CHECK(hasAction(actions, {1, 0}, kActivate));
        }
    }

    TEST_CASE("VK-1591: inter-ring hysteresis — a Prefetched sector between prefetch and unload is kept")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 6);

        // (3,0) center is 3 sectors out: beyond prefetchRadius (2), inside unloadRadius (4)
        manager.getSector({3, 0})->state = world::SectorState::Prefetched;

        world::SectorStreamingConfig config;
        config.loadRadius = 1.0f;
        config.prefetchRadius = 2.0f;
        config.unloadRadius = 4.0f;
        config.maxLoadsPerFrame = 8;
        config.maxPrefetchesPerFrame = 8;
        config.maxUnloadsPerFrame = 8;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true); // seeds (3,0) as tracked

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        CHECK_FALSE(hasAction(actions, {3, 0}, kUnload));
        CHECK_FALSE(hasAction(actions, {3, 0}, kActivate));
        CHECK_FALSE(hasAction(actions, {3, 0}, kPrefetch));
    }

    TEST_CASE("VK-1591: a Prefetched sector past unloadRadius is dropped; a Prefetching one is not")
    {
        world::SectorStreamingConfig config;
        config.loadRadius = 1.0f;
        config.prefetchRadius = 2.0f;
        config.unloadRadius = 4.0f;
        config.maxLoadsPerFrame = 0;
        config.maxPrefetchesPerFrame = 0;
        config.maxUnloadsPerFrame = 8;

        SUBCASE("Prefetched sectors are evictable — no dirty flag can ever protect them")
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 8);
            manager.getSector({6, 0})->state = world::SectorState::Prefetched;

            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
            std::vector<world::SectorStreamingAction> actions;

            streamer.update(sources, manager, actions);
            CHECK(hasAction(actions, {6, 0}, kUnload));
        }

        SUBCASE("Prefetching sectors are in flight and are never evicted")
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 8);
            manager.getSector({6, 0})->state = world::SectorState::Prefetching;

            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
            std::vector<world::SectorStreamingAction> actions;

            streamer.update(sources, manager, actions);
            CHECK_FALSE(hasAction(actions, {6, 0}, kUnload));
        }
    }

    TEST_CASE("VK-1591: a Loaded sector inside the prefetch ring is never demoted")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        populateGrid(manager, 6);

        // (3,0) is beyond loadRadius (2) but inside prefetchRadius (4): its ring target is
        // Prefetched, yet a spawned sector must keep its entities until unloadRadius.
        manager.getSector({3, 0})->state = world::SectorState::Loaded;

        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.prefetchRadius = 4.0f;
        config.unloadRadius = 5.0f;
        config.maxLoadsPerFrame = 8;
        config.maxPrefetchesPerFrame = 8;
        config.maxUnloadsPerFrame = 8;

        world::SectorStreamer streamer(config);
        streamer.setEnabled(true);

        std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
        std::vector<world::SectorStreamingAction> actions;

        streamer.update(sources, manager, actions);
        CHECK_FALSE(hasAction(actions, {3, 0}, kPrefetch));
        CHECK_FALSE(hasAction(actions, {3, 0}, kUnload));
        CHECK_FALSE(hasAction(actions, {3, 0}, kActivate));
    }

    TEST_CASE("VK-1591: a source's targetState caps what it can request")
    {
        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.prefetchRadius = 2.0f; // one ring — isolate the per-source cap
        config.unloadRadius = 4.0f;
        config.maxLoadsPerFrame = 32;
        config.maxPrefetchesPerFrame = 32;
        config.maxUnloadsPerFrame = 0;

        // Reference run: default targetState (Activated)
        std::vector<world::SectorCoord> activatedCoords;
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 4);
            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
            std::vector<world::SectorStreamingAction> actions;

            streamer.update(sources, manager, actions);
            REQUIRE_FALSE(actions.empty());
            for (const auto& action : actions)
            {
                CHECK(action.target == kActivate);
                activatedCoords.push_back(action.coord);
            }
        }

        // Same config, prefetch-only source: same coords, all as prefetch
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 4);
            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            auto source = sourceAtSectorCenter(0, 0);
            source.targetState = world::SectorTargetState::Prefetched;
            std::vector<world::StreamingSource> sources{source};
            std::vector<world::SectorStreamingAction> actions;

            streamer.update(sources, manager, actions);
            CHECK(actions.size() == activatedCoords.size());
            CHECK(countTargets(actions, kActivate) == 0);
            for (const auto& coord : activatedCoords)
                CHECK(hasAction(actions, coord, kPrefetch));
        }
    }

    TEST_CASE("VK-1591: a prefetch-only source never shadows a source that wants the sector Activated")
    {
        // Regression for the old visitedCoords first-claim: the highest-priority source whose
        // SCAN BOX contained a coord used to claim it before any distance test, so a
        // prefetch-only minimap source would downgrade the camera's own activation ring.
        world::SectorStreamingConfig config;
        config.loadRadius = 2.0f;
        config.prefetchRadius = 4.0f;
        config.unloadRadius = 6.0f;
        config.maxLoadsPerFrame = 32;
        config.maxPrefetchesPerFrame = 32;
        config.maxUnloadsPerFrame = 0;

        auto runWithPriorities = [&](uint8_t prefetchSourcePriority, uint8_t cameraPriority)
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 8);

            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);

            // Prefetch-only source at (6,0): (4,0) is 2 sectors away — inside its prefetch ring
            auto prefetchSource = sourceAtSectorCenter(6, 0);
            prefetchSource.targetState = world::SectorTargetState::Prefetched;
            prefetchSource.priority = prefetchSourcePriority;

            // Camera at (4,0): (4,0) is distance 0 — squarely inside its activate ring
            auto camera = sourceAtSectorCenter(4, 0);
            camera.priority = cameraPriority;

            std::vector<world::StreamingSource> sources{prefetchSource, camera};
            std::vector<world::SectorStreamingAction> actions;
            streamer.update(sources, manager, actions);
            return actions;
        };

        SUBCASE("the prefetch-only source outranks the camera")
        {
            auto actions = runWithPriorities(5, 0);
            CHECK(hasAction(actions, {4, 0}, kActivate));
            CHECK_FALSE(hasAction(actions, {4, 0}, kPrefetch));
        }

        SUBCASE("priorities swapped — target resolution is priority-independent")
        {
            auto actions = runWithPriorities(0, 5);
            CHECK(hasAction(actions, {4, 0}, kActivate));
            CHECK_FALSE(hasAction(actions, {4, 0}, kPrefetch));
        }
    }

    TEST_CASE("VK-1591: prefetch and activate draw on separate per-frame budgets")
    {
        world::SectorStreamingConfig config;
        config.loadRadius = 1.5f;
        config.prefetchRadius = 3.0f;
        config.unloadRadius = 4.0f;
        config.maxUnloadsPerFrame = 0;

        SUBCASE("one activate + two prefetches in the same frame")
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 4);
            config.maxLoadsPerFrame = 1;
            config.maxPrefetchesPerFrame = 2;

            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
            std::vector<world::SectorStreamingAction> actions;

            streamer.update(sources, manager, actions);
            CHECK(countTargets(actions, kActivate) == 1);
            CHECK(countTargets(actions, kPrefetch) == 2);
        }

        SUBCASE("a zero activate budget does not starve prefetching — they share no counter")
        {
            world::WorldSectorManager manager(makeSectorConfig());
            populateGrid(manager, 4);
            config.maxLoadsPerFrame = 0;
            config.maxPrefetchesPerFrame = 3;

            world::SectorStreamer streamer(config);
            streamer.setEnabled(true);
            std::vector<world::StreamingSource> sources{sourceAtSectorCenter(0, 0)};
            std::vector<world::SectorStreamingAction> actions;

            streamer.update(sources, manager, actions);
            CHECK(countTargets(actions, kActivate) == 0);
            CHECK(countTargets(actions, kPrefetch) == 3);
        }
    }

    TEST_CASE("VK-1591: normalizeConfig raises prefetchRadius to loadRadius and unloadRadius above it")
    {
        SUBCASE("a sub-loadRadius prefetch ring is absorbed")
        {
            world::SectorStreamingConfig config;
            config.loadRadius = 5.0f;
            config.prefetchRadius = 2.0f;
            config.unloadRadius = 3.0f;

            world::SectorStreamer streamer;
            streamer.setConfig(config);
            CHECK(streamer.getConfig().prefetchRadius >= streamer.getConfig().loadRadius);
            CHECK(streamer.getConfig().unloadRadius > streamer.getConfig().prefetchRadius);
        }

        SUBCASE("hysteresis sits outside the OUTERMOST residency ring, not just the activate ring")
        {
            world::SectorStreamingConfig config;
            config.loadRadius = 2.0f;
            config.prefetchRadius = 8.0f;
            config.unloadRadius = 6.0f; // already > loadRadius, but inside the prefetch ring

            world::SectorStreamer streamer;
            streamer.setConfig(config);
            CHECK(streamer.getConfig().unloadRadius > 8.0f);
        }

        SUBCASE("the 0 sentinel resolves to loadRadius and leaves hysteresis untouched")
        {
            world::SectorStreamingConfig config;
            config.loadRadius = 2.0f;
            config.unloadRadius = 3.0f; // today's tightest real config

            CHECK(world::effectivePrefetchRadius(config) == doctest::Approx(2.0f));

            world::SectorStreamer streamer(config);
            CHECK(streamer.getConfig().unloadRadius == doctest::Approx(3.0f));
        }
    }

    TEST_CASE("VK-1591: prefetchRadius == loadRadius emits no Prefetched action")
    {
        // The AC's "regression-equals all 17": replay the fixtures the existing cases use, with
        // prefetchRadius set EXPLICITLY to loadRadius, and require that the emitted action set is
        // byte-identical to the same run under the 0 sentinel — and contains no prefetch at all.
        struct Fixture
        {
            const char* name;
            float loadRadius;
            float unloadRadius;
            std::vector<world::StreamingSource> sources;
        };

        auto camera = sourceAtSectorCenter(0, 0);

        auto twoOnOneSector = [&]
        {
            std::vector<world::StreamingSource> s{sourceAtSectorCenter(0, 0), sourceAtSectorCenter(1, 0)};
            return s;
        };
        auto disjointPriorities = [&](uint8_t priority)
        {
            std::vector<world::StreamingSource> s{sourceAtSectorCenter(0, 0), sourceAtSectorCenter(10, 10)};
            s[1].priority = priority;
            return s;
        };

        const std::vector<Fixture> fixtures = {
            {"single source", 2.0f, 5.0f, {camera}},
            {"single source, tight hysteresis", 2.0f, 3.0f, {camera}},
            {"two equal-priority sources on adjacent sectors", 2.0f, 4.0f, twoOnOneSector()},
            {"disjoint source, priority 1", 2.0f, 4.0f, disjointPriorities(1)},
            {"disjoint source, priority 3", 2.0f, 4.0f, disjointPriorities(3)},
            {"radiusMultiplier 0.5 source", 1.5f, 2.5f, {sourceAtSectorCenter(5, 5, 0.5f)}},
        };

        for (const auto& fixture : fixtures)
        {
            CAPTURE(fixture.name);

            auto run = [&](float prefetchRadius)
            {
                world::WorldSectorManager manager(makeSectorConfig());
                populateGrid(manager, 12);

                world::SectorStreamingConfig config;
                config.loadRadius = fixture.loadRadius;
                config.prefetchRadius = prefetchRadius;
                config.unloadRadius = fixture.unloadRadius;
                config.maxLoadsPerFrame = 4;
                config.maxPrefetchesPerFrame = 4;
                config.maxUnloadsPerFrame = 4;

                world::SectorStreamer streamer(config);
                streamer.setEnabled(true);

                // Several frames so loads settle and the unload pass gets exercised
                std::vector<world::SectorStreamingAction> all;
                std::vector<world::SectorStreamingAction> actions;
                for (int frame = 0; frame < 4; ++frame)
                {
                    streamer.update(fixture.sources, manager, actions);
                    for (const auto& action : actions)
                    {
                        all.push_back(action);
                        // Mimic the service: an activation moves the sector to Loading
                        if (action.target == kActivate)
                            manager.getSector(action.coord)->state = world::SectorState::Loading;
                    }
                }
                return all;
            };

            const auto sentinel = run(0.0f);
            const auto explicitEqual = run(fixture.loadRadius);

            CHECK(countTargets(sentinel, kPrefetch) == 0);
            CHECK(countTargets(explicitEqual, kPrefetch) == 0);

            REQUIRE(sentinel.size() == explicitEqual.size());
            for (size_t i = 0; i < sentinel.size(); ++i)
            {
                CHECK(sentinel[i].coord == explicitEqual[i].coord);
                CHECK(sentinel[i].target == explicitEqual[i].target);
            }
        }
    }
}
