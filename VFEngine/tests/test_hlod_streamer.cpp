#include <doctest.h>
#include <world/WorldTypes.hpp>
#include <world/HLODTypes.hpp>
#include <world/WorldSectorManager.hpp>
#include <world/HLODStreamer.hpp>

#include <algorithm>
#include <utility>
#include <vector>

// ============================================================
// HLODStreamer decision logic (tier bands, cell mapping,
// proxy suppression while base sectors are loaded, hysteresis)
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

    world::SectorStreamingConfig makeStreamConfig()
    {
        world::SectorStreamingConfig config;
        config.loadRadius = 4.0f;
        config.unloadRadius = 5.0f; // proxies only beyond 500 world units
        return config;
    }

    world::HLODConfig singleTierConfig(uint8_t cellSize, float displayRadius)
    {
        world::HLODConfig config;
        config.enabled = true;
        config.tiers = {{0, cellSize, displayRadius, 0.1f}};
        return config;
    }

    world::StreamingSource sourceAt(float x, float z)
    {
        world::StreamingSource source;
        source.position = glm::vec3(x, 0.0f, z);
        return source;
    }

    bool hasAction(const std::vector<world::HLODStreamingAction>& actions,
                   const world::HLODCellCoord& cell, bool isLoad)
    {
        return std::any_of(actions.begin(), actions.end(),
            [&](const world::HLODStreamingAction& a)
            { return a.cellCoord == cell && a.isLoad == isLoad; });
    }
}

TEST_SUITE("HLODStreamer")
{
    TEST_CASE("disabled HLOD config emits nothing")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;
        world::HLODConfig hlod = singleTierConfig(1, 10.0f);
        hlod.enabled = false;
        streamer.setConfig(makeStreamConfig(), hlod);

        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);

        CHECK(actions.empty());
        CHECK(streamer.getLoadedProxies().empty());
    }

    TEST_CASE("proxies load only in the band between unloadRadius and displayRadius")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;
        streamer.setConfig(makeStreamConfig(), singleTierConfig(1, 10.0f));

        // Source at center of sector (0,0); cells on the +X axis have center
        // distance x * 100. Band: [500, 1000].
        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);

        CHECK(hasAction(actions, {7, 0, 0}, true));        // 700: inside band
        CHECK_FALSE(hasAction(actions, {2, 0, 0}, true));  // 200: inside unload radius
        CHECK_FALSE(hasAction(actions, {12, 0, 0}, true)); // 1200: beyond display radius

        CHECK(streamer.getLoadedProxies().contains(world::HLODCellCoord(7, 0, 0)));
    }

    TEST_CASE("cellSize 2 maps multiple sectors to one cell")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;
        streamer.setConfig(makeStreamConfig(), singleTierConfig(2, 10.0f));

        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);

        // Cell (3,0) at cellSize 2 covers sectors (6..7, 0..1); center distance 700
        CHECK(hasAction(actions, {3, 0, 0}, true));
    }

    TEST_CASE("proxy suppressed while any covered sector is loaded")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        auto& sector = manager.getOrCreateSector({7, 0});
        sector.state = world::SectorState::Loaded;

        world::HLODStreamer streamer;
        streamer.setConfig(makeStreamConfig(), singleTierConfig(1, 10.0f));

        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);

        CHECK_FALSE(hasAction(actions, {7, 0, 0}, true)); // real geometry wins
        CHECK(hasAction(actions, {8, 0, 0}, true));       // neighbors unaffected
    }

    TEST_CASE("proxy unloads when its sector streams in")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        auto& pending = manager.getOrCreateSector({7, 0}); // exists but unloaded
        CHECK(pending.state == world::SectorState::Unloaded);

        world::HLODStreamer streamer;
        streamer.setConfig(makeStreamConfig(), singleTierConfig(1, 10.0f));

        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);
        REQUIRE(streamer.getLoadedProxies().contains(world::HLODCellCoord(7, 0, 0)));

        manager.getSector({7, 0})->state = world::SectorState::Loaded;

        actions.clear();
        streamer.update(sources, manager, makeSectorConfig(), actions);
        CHECK(hasAction(actions, {7, 0, 0}, false));
        CHECK_FALSE(streamer.getLoadedProxies().contains(world::HLODCellCoord(7, 0, 0)));
    }

    TEST_CASE("hysteresis keeps proxies loaded slightly beyond display radius")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;
        streamer.setConfig(makeStreamConfig(), singleTierConfig(1, 10.0f));

        // Cell (10,0): center at x=1050. Source at x=50 -> distance exactly 1000 (= radius)
        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);
        REQUIRE(streamer.getLoadedProxies().contains(world::HLODCellCoord(10, 0, 0)));

        // Step back 50: distance 1050, beyond radius (1000) but inside hysteresis (1100)
        sources[0] = sourceAt(0.0f, 50.0f);
        actions.clear();
        streamer.update(sources, manager, makeSectorConfig(), actions);
        CHECK_FALSE(hasAction(actions, {10, 0, 0}, false));
        CHECK(streamer.getLoadedProxies().contains(world::HLODCellCoord(10, 0, 0)));

        // Step back further: distance 1250, beyond hysteresis -> unload
        sources[0] = sourceAt(-200.0f, 50.0f);
        actions.clear();
        streamer.update(sources, manager, makeSectorConfig(), actions);
        CHECK(hasAction(actions, {10, 0, 0}, false));
        CHECK_FALSE(streamer.getLoadedProxies().contains(world::HLODCellCoord(10, 0, 0)));
    }

    TEST_CASE("proxies stay loaded across updates without duplicate load actions")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;
        streamer.setConfig(makeStreamConfig(), singleTierConfig(1, 10.0f));

        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);
        REQUIRE(hasAction(actions, {7, 0, 0}, true));

        actions.clear();
        streamer.update(sources, manager, makeSectorConfig(), actions);
        CHECK_FALSE(hasAction(actions, {7, 0, 0}, true)); // already loaded, no re-emit
    }

    TEST_CASE("forgetProxy re-emits the load on the next update (invalidation path)")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;
        streamer.setConfig(makeStreamConfig(), singleTierConfig(1, 10.0f));

        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);
        REQUIRE(streamer.getLoadedProxies().contains(world::HLODCellCoord(7, 0, 0)));

        // HLOD bake invalidated: tracking dropped, so a fresh bake gets re-requested
        streamer.forgetProxy(world::HLODCellCoord(7, 0, 0));
        CHECK_FALSE(streamer.getLoadedProxies().contains(world::HLODCellCoord(7, 0, 0)));

        actions.clear();
        streamer.update(sources, manager, makeSectorConfig(), actions);
        CHECK(hasAction(actions, {7, 0, 0}, true));
    }

    TEST_CASE("clear drops all loaded proxies")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;
        streamer.setConfig(makeStreamConfig(), singleTierConfig(1, 10.0f));

        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);
        REQUIRE_FALSE(streamer.getLoadedProxies().empty());

        streamer.clear();
        CHECK(streamer.getLoadedProxies().empty());
    }

    TEST_CASE("multiple tiers track proxies independently by tier id")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;

        world::HLODConfig hlod;
        hlod.enabled = true;
        hlod.tiers = {
            {0, 1, 10.0f, 0.10f},
            {1, 2, 20.0f, 0.03f}};
        streamer.setConfig(makeStreamConfig(), hlod);

        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);

        // Tier 0 owns [500, 1000]: cell (7,0) sits at 700
        CHECK(hasAction(actions, {7, 0, 0}, true));

        // VK-1594: tier 1 owns the ANNULUS (1000, 2000], so it must NOT also claim the area
        // tier 0 already covers. Tier-1 cell (3,0) centres at ~652 - inside tier 0's disc.
        // Before the annulus fix both tiers loaded it and the proxies rendered stacked.
        CHECK_FALSE(hasAction(actions, {3, 0, 1}, true));

        // Tier 1 reaches beyond tier 0's display radius: cell (7,0) tier 1 centre ~1451
        CHECK(hasAction(actions, {7, 0, 1}, true));
        CHECK_FALSE(hasAction(actions, {15, 0, 0}, true));
    }

    TEST_CASE("VK-1594: every cell is claimed by at most one tier")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;

        world::HLODConfig hlod;
        hlod.enabled = true;
        hlod.tiers = {
            {0, 1, 10.0f, 0.10f},
            {1, 2, 20.0f, 0.03f},
            {2, 4, 40.0f, 0.01f}};
        streamer.setConfig(makeStreamConfig(), hlod);

        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);

        // Every loaded cell maps back to a world-space footprint. Two tiers covering the same
        // ground is the double-draw bug, so assert the annuli are disjoint by distance: a cell's
        // centre distance must lie in its own tier's band and no other's. Compared squared, the
        // way the streamer does, so a cell sitting exactly on a boundary cannot fail on a sqrt
        // rounding difference.
        const float unloadR = makeStreamConfig().unloadRadius;
        auto bandSqFor = [&](uint8_t tier)
        {
            float lo = unloadR, hi = 10.0f;
            if (tier == 1) { lo = 10.0f; hi = 20.0f; }
            else if (tier == 2) { lo = 20.0f; hi = 40.0f; }
            lo *= kSectorSize;
            hi *= kSectorSize;
            return std::pair<float, float>{lo * lo, hi * hi};
        };

        REQUIRE_FALSE(streamer.getLoadedProxies().empty());
        bool sawTier0 = false, sawTier1 = false, sawTier2 = false;
        for (const auto& cell : streamer.getLoadedProxies())
        {
            const float cs = (cell.tier == 0) ? 1.0f : (cell.tier == 1 ? 2.0f : 4.0f);
            const float cx = (static_cast<float>(cell.x) * cs + cs * 0.5f) * kSectorSize;
            const float cz = (static_cast<float>(cell.z) * cs + cs * 0.5f) * kSectorSize;
            const float dx = cx - 50.0f;
            const float dz = cz - 50.0f;
            const float distSq = dx * dx + dz * dz;

            auto [loSq, hiSq] = bandSqFor(cell.tier);
            CHECK(distSq >= loSq);
            CHECK(distSq <= hiSq);

            if (cell.tier == 0) sawTier0 = true;
            else if (cell.tier == 1) sawTier1 = true;
            else if (cell.tier == 2) sawTier2 = true;
        }

        // All three tiers must actually be exercised, or the disjointness check above is vacuous
        CHECK(sawTier0);
        CHECK(sawTier1);
        CHECK(sawTier2);
    }

    TEST_CASE("VK-1594: pulling back hands a cell off from tier 0 to tier 1")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;

        world::HLODConfig hlod;
        hlod.enabled = true;
        hlod.tiers = {
            {0, 1, 10.0f, 0.10f},
            {1, 2, 20.0f, 0.03f}};
        streamer.setConfig(makeStreamConfig(), hlod);

        // Tier-1 cell (3,0) centres at x=700. Standing near it, tier 1 must not own it.
        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);
        REQUIRE_FALSE(streamer.getLoadedProxies().contains(world::HLODCellCoord(3, 0, 1)));

        // Retreat far enough that cell (3,0) falls into tier 1's annulus (>1000 away).
        sources[0] = sourceAt(-500.0f, 50.0f); // distance to x=700 is 1200
        actions.clear();
        streamer.update(sources, manager, makeSectorConfig(), actions);
        CHECK(hasAction(actions, {3, 0, 1}, true));
        CHECK(streamer.getLoadedProxies().contains(world::HLODCellCoord(3, 0, 1)));
    }

    TEST_CASE("VK-1594: a proxy the camera moves inside of is released")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;
        streamer.setConfig(makeStreamConfig(), singleTierConfig(1, 10.0f));

        // Cell (7,0) centres at x=750; from x=50 that is 700, inside the [500,1000] band.
        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);
        REQUIRE(streamer.getLoadedProxies().contains(world::HLODCellCoord(7, 0, 0)));

        // Walk onto the cell: distance 50, well inside the inner hysteresis edge (400).
        // Before VK-1594 this proxy stayed resident forever - it left wantLoaded but the unload
        // test only ever asked whether it was beyond the OUTER radius.
        sources[0] = sourceAt(700.0f, 50.0f);
        actions.clear();
        streamer.update(sources, manager, makeSectorConfig(), actions);
        CHECK(hasAction(actions, {7, 0, 0}, false));
        CHECK_FALSE(streamer.getLoadedProxies().contains(world::HLODCellCoord(7, 0, 0)));
    }

    TEST_CASE("VK-1594: tier order is taken from displayRadius, not the tier field")
    {
        world::WorldSectorManager manager(makeSectorConfig());
        world::HLODStreamer streamer;

        // Same two tiers as above but listed coarse-first, as a hand-edited .vfworld might.
        world::HLODConfig hlod;
        hlod.enabled = true;
        hlod.tiers = {
            {1, 2, 20.0f, 0.03f},
            {0, 1, 10.0f, 0.10f}};
        streamer.setConfig(makeStreamConfig(), hlod);

        std::vector<world::StreamingSource> sources{sourceAt(50.0f, 50.0f)};
        std::vector<world::HLODStreamingAction> actions;
        streamer.update(sources, manager, makeSectorConfig(), actions);

        // Identical outcome to the sorted listing: tier 1 still starts where tier 0 ends.
        CHECK(hasAction(actions, {7, 0, 0}, true));
        CHECK_FALSE(hasAction(actions, {3, 0, 1}, true));
        CHECK(hasAction(actions, {7, 0, 1}, true));
    }
}
