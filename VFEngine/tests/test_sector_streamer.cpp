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
                     const world::SectorCoord& coord, bool isLoad)
    {
        return static_cast<int>(std::count_if(actions.begin(), actions.end(),
            [&](const world::SectorStreamingAction& a)
            { return a.coord == coord && a.isLoad == isLoad; }));
    }

    bool hasAction(const std::vector<world::SectorStreamingAction>& actions,
                   const world::SectorCoord& coord, bool isLoad)
    {
        return countActions(actions, coord, isLoad) > 0;
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
        CHECK(actions[0].isLoad);
        CHECK(actions[0].coord == world::SectorCoord(0, 0)); // distance 0 from source

        // Simulate the service marking the sector as loading; next frame picks an
        // axis-aligned neighbor (all at distance == one sector size, nearest remaining)
        manager.getSector(actions[0].coord)->state = world::SectorState::Loading;

        actions.clear();
        streamer.update(sources, manager, actions);
        REQUIRE(actions.size() == 1);
        CHECK(actions[0].isLoad);
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
            CHECK(action.isLoad);
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
        CHECK_FALSE(hasAction(actions, {3, 0}, false));
        CHECK_FALSE(hasAction(actions, {3, 0}, true));
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
        CHECK_FALSE(actions[0].isLoad);
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
        CHECK_FALSE(hasAction(actions, {6, 0}, false));
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

        CHECK(countActions(actions, {0, 0}, true) == 1);
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
            CHECK(hasAction(actions, {0, 0}, true));
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
        CHECK_FALSE(hasAction(actions, {10, 0}, false));

        // Remove source B: now outside ALL unload radii -> unloads
        sources.pop_back();
        actions.clear();
        streamer.update(sources, manager, actions);
        CHECK(hasAction(actions, {10, 0}, false));
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
        CHECK(hasAction(actions, {10, 10}, false));
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
        CHECK_FALSE(hasAction(actions, {10, 10}, false));
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
            CHECK(actions[0].isLoad);
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
            CHECK(actions[0].isLoad);
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
            CHECK(actions[0].isLoad);
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
            CHECK(actions[0].isLoad);
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

            CHECK(hasAction(actions, {5, 5}, true));
            // 75 < 100, so the axis neighbours stay out of reach
            CHECK_FALSE(hasAction(actions, {4, 5}, true));
            CHECK_FALSE(hasAction(actions, {6, 5}, true));
            CHECK_FALSE(hasAction(actions, {5, 4}, true));
            CHECK_FALSE(hasAction(actions, {5, 6}, true));
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
            CHECK_FALSE(hasAction(actions, {5, 5}, false));

            // Drop the base source (its owner died): the camera alone evicts it again.
            sources.pop_back();
            actions.clear();
            streamer.update(sources, manager, actions);
            CHECK(hasAction(actions, {5, 5}, false));
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
    }
}
