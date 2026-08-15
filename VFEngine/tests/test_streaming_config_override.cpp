#include <doctest.h>
#include <world/WorldTypes.hpp>
#include <world/SectorStreamer.hpp>

#include <cmath>
#include <limits>
#include <optional>

// ============================================================
// VK-1595: the session streaming override's decision helper, and the
// normalizeStreamingConfig extraction that made it possible for the service
// to validate a config it is NOT handing to the streamer.
// ============================================================

namespace
{
    // Every field distinct from the struct defaults, so a helper that silently substituted a
    // default-constructed config would fail rather than accidentally match.
    world::SectorStreamingConfig makeDistinctConfig()
    {
        world::SectorStreamingConfig config;
        config.loadRadius = 7.0f;
        config.prefetchRadius = 9.0f;
        config.unloadRadius = 11.0f;
        config.maxLoadsPerFrame = 3;
        config.maxPrefetchesPerFrame = 5;
        config.maxUnloadsPerFrame = 2;
        config.maxEntitiesPerFrame = 24;
        config.maxTerrainLoadsPerFrame = 6;
        config.maxTerrainUnloadsPerFrame = 7;
        config.maxPrefetchBytes = 1234567;
        config.maxHLODProxyBytes = 7654321; // VK-1600
        config.maxLoadedSectors = 77;       // VK-1600
        config.lookaheadSeconds = 1.5f;
        config.viewBiasStrength = 2.0f;
        config.teleportThresholdSectors = 3.0f;
        config.burstFrames = 42;
        config.maxLoadsPerFrameBurst = 17;
        config.maxEntitiesPerFrameBurst = 96;
        config.enableGPUObjectStreaming = false;
        config.editModeStreaming = true;
        config.hlodTier0Radius = 12.0f;
        config.hlodTier1Radius = 24.0f;
        config.hlodTier2Radius = 48.0f;
        return config;
    }

    bool sameConfig(const world::SectorStreamingConfig& a, const world::SectorStreamingConfig& b)
    {
        return a.loadRadius == b.loadRadius
            && a.prefetchRadius == b.prefetchRadius
            && a.unloadRadius == b.unloadRadius
            && a.maxLoadsPerFrame == b.maxLoadsPerFrame
            && a.maxPrefetchesPerFrame == b.maxPrefetchesPerFrame
            && a.maxUnloadsPerFrame == b.maxUnloadsPerFrame
            && a.maxEntitiesPerFrame == b.maxEntitiesPerFrame
            && a.maxTerrainLoadsPerFrame == b.maxTerrainLoadsPerFrame
            && a.maxTerrainUnloadsPerFrame == b.maxTerrainUnloadsPerFrame
            && a.maxPrefetchBytes == b.maxPrefetchBytes
            && a.maxHLODProxyBytes == b.maxHLODProxyBytes // VK-1600
            && a.maxLoadedSectors == b.maxLoadedSectors   // VK-1600
            && a.lookaheadSeconds == b.lookaheadSeconds
            && a.viewBiasStrength == b.viewBiasStrength
            && a.teleportThresholdSectors == b.teleportThresholdSectors
            && a.burstFrames == b.burstFrames
            && a.maxLoadsPerFrameBurst == b.maxLoadsPerFrameBurst
            && a.maxEntitiesPerFrameBurst == b.maxEntitiesPerFrameBurst
            && a.enableGPUObjectStreaming == b.enableGPUObjectStreaming
            && a.editModeStreaming == b.editModeStreaming
            && a.hlodTier0Radius == b.hlodTier0Radius
            && a.hlodTier1Radius == b.hlodTier1Radius
            && a.hlodTier2Radius == b.hlodTier2Radius;
    }
}

TEST_SUITE("StreamingConfigOverride")
{
    TEST_CASE("absent override passes the persisted config through")
    {
        const world::SectorStreamingConfig persisted = makeDistinctConfig();
        const std::optional<world::SectorStreamingConfig> noOverride;

        const auto& result = world::effectiveStreamingConfig(noOverride, persisted);

        // Identity, not equality: the helper must hand back the caller's own object. A copy would
        // let a caller mutate the result believing it had written to worldDefinition - or worse,
        // round-trip an override back into the persisted config.
        CHECK(&result == &persisted);
        CHECK(sameConfig(result, persisted));
    }

    TEST_CASE("override wins over the persisted config")
    {
        const world::SectorStreamingConfig persisted; // struct defaults
        world::SectorStreamingConfig overrideConfig = makeDistinctConfig();
        const std::optional<world::SectorStreamingConfig> sessionOverride = overrideConfig;

        const auto& result = world::effectiveStreamingConfig(sessionOverride, persisted);

        CHECK(&result == &(*sessionOverride));
        CHECK(sameConfig(result, overrideConfig));
        CHECK(result.loadRadius != persisted.loadRadius);
    }

    TEST_CASE("override never mutates the persisted config")
    {
        world::SectorStreamingConfig persisted = makeDistinctConfig();
        const world::SectorStreamingConfig persistedBefore = persisted;

        world::SectorStreamingConfig overrideConfig;
        overrideConfig.loadRadius = 1.0f;
        overrideConfig.unloadRadius = 2.0f;
        const std::optional<world::SectorStreamingConfig> sessionOverride = overrideConfig;

        const auto& result = world::effectiveStreamingConfig(sessionOverride, persisted);
        CHECK(result.loadRadius == doctest::Approx(1.0f));

        // This is the ".vfworld diff proves nothing persisted" property in miniature.
        CHECK(sameConfig(persisted, persistedBefore));
    }

    TEST_CASE("normalizeStreamingConfig resolves the prefetch sentinel")
    {
        world::SectorStreamingConfig config;
        config.loadRadius = 6.0f;
        config.prefetchRadius = 0.0f; // the "no prefetch ring" sentinel
        config.unloadRadius = 9.0f;

        world::normalizeStreamingConfig(config);

        CHECK(config.prefetchRadius == doctest::Approx(6.0f));
        CHECK(config.unloadRadius == doctest::Approx(9.0f)); // already outside, untouched
    }

    TEST_CASE("normalizeStreamingConfig pushes hysteresis past the OUTERMOST ring")
    {
        world::SectorStreamingConfig config;
        config.loadRadius = 4.0f;
        config.prefetchRadius = 8.0f;
        config.unloadRadius = 5.0f; // outside loadRadius but INSIDE the prefetch ring

        world::normalizeStreamingConfig(config);

        CHECK(config.prefetchRadius == doctest::Approx(8.0f));
        CHECK(config.unloadRadius == doctest::Approx(9.0f));
    }

    TEST_CASE("normalizeStreamingConfig leaves a coherent config untouched")
    {
        world::SectorStreamingConfig config = makeDistinctConfig();
        const world::SectorStreamingConfig before = config;

        world::normalizeStreamingConfig(config);

        CHECK(sameConfig(config, before));
    }

    TEST_CASE("normalizeStreamingConfig matches SectorStreamer::setConfig")
    {
        // The extraction is only safe if the streamer still resolves a config exactly as it did
        // before VK-1595. Replay the interesting shapes through both paths.
        struct Case { float load; float prefetch; float unload; };
        const Case cases[] = {
            {4.0f, 0.0f, 5.0f},   // the default shape: sentinel prefetch
            {6.0f, 0.0f, 9.0f},
            {4.0f, 8.0f, 5.0f},   // hysteresis inside the prefetch ring
            {4.0f, 2.0f, 5.0f},   // prefetch below load
            {2.0f, 2.0f, 2.0f},   // degenerate: everything equal
            {10.0f, 12.0f, 20.0f} // already coherent
        };

        for (const auto& c : cases)
        {
            world::SectorStreamingConfig input;
            input.loadRadius = c.load;
            input.prefetchRadius = c.prefetch;
            input.unloadRadius = c.unload;

            world::SectorStreamingConfig expected = input;
            world::normalizeStreamingConfig(expected);

            world::SectorStreamer streamer;
            streamer.setConfig(input);

            CHECK(sameConfig(streamer.getConfig(), expected));
        }
    }

    // ---- VK-1600 review: sanitizeStreamingConfig ----
    //
    // normalizeStreamingConfig enforces ring COHERENCE and assumes sane inputs. Nothing on the
    // .vfworld read path guaranteed that, and the editor sliders - the only other bound - cover
    // neither a hand-edited file, nor a plugin, nor a script SetStreamingConfigCommand.

    TEST_CASE("sanitizeStreamingConfig PRESERVES every documented 0 sentinel")
    {
        // The whole point of sanitizing rather than normalizing at the persistence boundary.
        world::SectorStreamingConfig config;
        config.loadRadius = 6.0f;
        config.prefetchRadius = 0.0f;          // "no prefetch ring"
        config.teleportThresholdSectors = 0.0f; // "use kDefaultTeleportThresholdSectors"
        config.maxLoadsPerFrameBurst = 0;       // "4x maxLoadsPerFrame"
        config.maxEntitiesPerFrameBurst = 0;    // ditto
        config.maxPrefetchBytes = 0;            // "unlimited"

        world::sanitizeStreamingConfig(config);

        CHECK(config.prefetchRadius == doctest::Approx(0.0f));
        CHECK(config.teleportThresholdSectors == doctest::Approx(0.0f));
        CHECK(config.maxLoadsPerFrameBurst == 0);
        CHECK(config.maxEntitiesPerFrameBurst == 0);
        CHECK(config.maxPrefetchBytes == 0);
        CHECK(config.loadRadius == doctest::Approx(6.0f));
    }

    TEST_CASE("sanitizeStreamingConfig floors a negative per-frame budget at 0")
    {
        // The crash this exists to stop: SectorStreamer sums three std::min(int, budget) terms
        // into reserve(), so ONE negative budget made the sum negative and reserve() sign-extended
        // it into a near-SIZE_MAX request - std::length_error out of a main-thread-pinned frame
        // task with no handler above it.
        world::SectorStreamingConfig config;
        config.maxLoadsPerFrame = -1;
        config.maxPrefetchesPerFrame = -100;
        config.maxUnloadsPerFrame = -1;
        config.maxEntitiesPerFrame = -8;
        config.burstFrames = -5;

        world::sanitizeStreamingConfig(config);

        CHECK(config.maxLoadsPerFrame == 0);
        CHECK(config.maxPrefetchesPerFrame == 0);
        CHECK(config.maxUnloadsPerFrame == 0);
        CHECK(config.maxEntitiesPerFrame == 0);
        CHECK(config.burstFrames == 0);

        // effectiveBurstLoads multiplies maxLoadsPerFrame by 4; the clamp keeps that in range.
        CHECK(world::effectiveBurstLoads(config) == 0);
    }

    TEST_CASE("sanitizeStreamingConfig caps an absurd per-frame budget")
    {
        world::SectorStreamingConfig config;
        config.maxLoadsPerFrame = 1'000'000'000;

        world::sanitizeStreamingConfig(config);

        CHECK(config.maxLoadsPerFrame == world::kMaxPerFrameStreamingBudget);
        // The product must still be a sane int rather than signed-overflow UB.
        CHECK(world::effectiveBurstLoads(config) == world::kMaxPerFrameStreamingBudget * 4);
    }

    TEST_CASE("sanitizeStreamingConfig repairs a negative or NaN radius")
    {
        // A negative loadRadius is a SILENT TOTAL STALL, not a small ring: SectorStreamer's scan
        // box inverts (minWorldX > maxWorldX) so nothing ever loads, while the unload test squares
        // the radius back into a large positive so distSq <= unloadRadiusSq always holds and
        // nothing ever unloads. normalizeStreamingConfig cannot catch it - and NaN slips through
        // that function entirely, because every comparison against NaN is false.
        const world::SectorStreamingConfig defaults;

        world::SectorStreamingConfig negative;
        negative.loadRadius = -100.0f;
        negative.unloadRadius = -99.0f;
        world::sanitizeStreamingConfig(negative);
        CHECK(negative.loadRadius == doctest::Approx(defaults.loadRadius));
        CHECK(negative.unloadRadius == doctest::Approx(defaults.unloadRadius));

        world::SectorStreamingConfig nan;
        nan.loadRadius = std::numeric_limits<float>::quiet_NaN();
        nan.unloadRadius = std::numeric_limits<float>::quiet_NaN();
        nan.prefetchRadius = std::numeric_limits<float>::quiet_NaN();
        world::sanitizeStreamingConfig(nan);
        CHECK(std::isfinite(nan.loadRadius));
        CHECK(std::isfinite(nan.unloadRadius));
        CHECK(nan.prefetchRadius == doctest::Approx(0.0f)); // folds TO the sentinel

        // And the pair is still coherent once normalize runs on top, as the streamer will do.
        world::normalizeStreamingConfig(nan);
        CHECK(nan.unloadRadius > nan.prefetchRadius);
    }

    TEST_CASE("sanitizeStreamingConfig caps a runaway radius")
    {
        // SectorStreamer derives its scan box straight from the radius with no independent cap,
        // so a huge radius is a per-frame runaway loop, not merely a large working set.
        world::SectorStreamingConfig config;
        config.loadRadius = 1e9f;
        config.prefetchRadius = 1e9f;

        world::sanitizeStreamingConfig(config);

        CHECK(config.loadRadius == doctest::Approx(world::kMaxStreamingRadiusSectors));
        CHECK(config.prefetchRadius == doctest::Approx(world::kMaxStreamingRadiusSectors));
    }

    TEST_CASE("sanitizeStreamingConfig leaves a sane config untouched")
    {
        world::SectorStreamingConfig config = makeDistinctConfig();
        const world::SectorStreamingConfig before = config;

        world::sanitizeStreamingConfig(config);

        CHECK(sameConfig(config, before));
    }

    TEST_CASE("sanitizeSectorConfig repairs a divisor that would produce inf coords")
    {
        const world::SectorConfig defaults;

        world::SectorConfig zero;
        zero.sectorWorldSize = 0.0f;
        world::sanitizeSectorConfig(zero);
        CHECK(zero.sectorWorldSize == doctest::Approx(defaults.sectorWorldSize));

        world::SectorConfig negative;
        negative.sectorWorldSize = -128.0f;
        negative.tilesPerSector = 0;
        world::sanitizeSectorConfig(negative);
        CHECK(negative.sectorWorldSize == doctest::Approx(defaults.sectorWorldSize));
        CHECK(negative.tilesPerSector == defaults.tilesPerSector);

        world::SectorConfig nan;
        nan.sectorWorldSize = std::numeric_limits<float>::quiet_NaN();
        world::sanitizeSectorConfig(nan);
        CHECK(std::isfinite(nan.sectorWorldSize));
    }
}
