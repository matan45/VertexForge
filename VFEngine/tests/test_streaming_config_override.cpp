#include <doctest.h>
#include <world/WorldTypes.hpp>
#include <world/SectorStreamer.hpp>

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
}
