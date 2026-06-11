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
