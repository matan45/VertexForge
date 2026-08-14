#pragma once

#include <cstdint>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <glm/glm.hpp>

namespace world
{
    struct SectorCoord
    {
        int32_t x = 0;
        int32_t z = 0;

        SectorCoord() = default;
        constexpr SectorCoord(int32_t x, int32_t z) : x(x), z(z) {}

        bool operator==(const SectorCoord& other) const
        {
            return x == other.x && z == other.z;
        }

        bool operator!=(const SectorCoord& other) const
        {
            return !(*this == other);
        }

        SectorCoord operator+(const SectorCoord& other) const
        {
            return SectorCoord(x + other.x, z + other.z);
        }

        SectorCoord operator-(const SectorCoord& other) const
        {
            return SectorCoord(x - other.x, z - other.z);
        }
    };

    struct SectorCoordHash
    {
        size_t operator()(const SectorCoord& coord) const
        {
            size_t h1 = std::hash<int32_t>{}(coord.x);
            size_t h2 = std::hash<int32_t>{}(coord.z);
            return h1 ^ (h2 << 1);
        }
    };

    // A sector id packs each axis into 16 bits, so a coord is only uniquely addressable over a
    // 65536-wide window per axis. Biasing by 0x8000 centres that window on the origin - the full
    // int16 range. Outside it two distinct sectors collapse onto the same id and silently share a
    // GPU streaming slot, so validate with isValidSectorCoord() before creating a sector.
    inline constexpr int32_t kMinSectorCoord = -32768;
    inline constexpr int32_t kMaxSectorCoord = 32767;

    [[nodiscard]] inline constexpr bool isValidSectorCoord(const SectorCoord& coord) noexcept
    {
        return coord.x >= kMinSectorCoord && coord.x <= kMaxSectorCoord
            && coord.z >= kMinSectorCoord && coord.z <= kMaxSectorCoord;
    }

    // Bit concatenation (not a pairing function): biased x in the high 16 bits, biased z in the
    // low 16. Injective exactly over [kMinSectorCoord, kMaxSectorCoord] on both axes.
    //
    // VK-1588 re-biased this from 0x4000, whose window was the lopsided [-16384, +49151]. Safe
    // because the id is runtime-only: .vfworld/.vfsector key sectors on {x,z}, .vfsector filenames
    // and .vfHLOD paths are coord-derived, and .vfpak is path-keyed. The only consumers are the
    // transient in-memory maps in LightStreamManager and GPUObjectStreamManager.
    //
    // The int32 -> uint32 cast happens BEFORE the bias so an out-of-range coord wraps under
    // defined unsigned arithmetic instead of overflowing a signed int (UB).
    [[nodiscard]] inline uint32_t sectorCoordToId(const SectorCoord& coord) noexcept
    {
        const uint32_t ux = (static_cast<uint32_t>(coord.x) + 0x8000u) & 0xFFFFu;
        const uint32_t uz = (static_cast<uint32_t>(coord.z) + 0x8000u) & 0xFFFFu;
        return (ux << 16) | uz;
    }

    // VK-1599: the maximum number of runtime grids a world may declare. Grid indices are uint8_t
    // everywhere, but the cap is far tighter than that: every grid is a full WorldSectorManager +
    // SectorStreamer with its own per-frame budgets, and grid 0 is the only one that drives
    // terrain / ocean / navmesh / HLOD.
    inline constexpr uint8_t kMaxGrids = 8;

    // VK-1599: grid 0, the one every world has and the only one that drives terrain, ocean,
    // navmesh and HLOD. Named rather than written as a bare 0 at the call sites that are
    // deliberately primary-grid-only, so they stay distinguishable from ones simply not yet
    // converted.
    inline constexpr uint8_t kPrimaryGridIndex = 0;

    // VK-1599: the key every runtime registration is made under - GPU object slots, streamed light
    // slots, ResourceLoadScheduler hints. sectorCoordToId already consumes all 32 bits over the
    // validated coord range, so a second grid at the same coord would silently share the first
    // grid's slot; the grid index has to live above it.
    //
    // 64 bits rather than a narrower coord window on purpose: shrinking the coord range would undo
    // VK-1588's deliberate widening to the full int16 span. Grid 0 reproduces the legacy
    // sectorCoordToId value exactly, so a single-grid world registers under the ids it always did.
    //
    // Runtime-only, like sectorCoordToId - nothing persists it (see the note above).
    [[nodiscard]] inline uint64_t sectorRegistrationId(uint8_t gridIndex,
                                                       const SectorCoord& coord) noexcept
    {
        return (static_cast<uint64_t>(gridIndex) << 32) | sectorCoordToId(coord);
    }

    // VK-1599: the single source of truth for a sector's on-disk name, replacing the two
    // byte-identical string builds that had drifted apart in WorldSectorPersistenceOps and
    // WorldSectorRepartitionOps.
    //
    // The primary grid keeps the historical spelling exactly, so a world that only ever uses one
    // grid produces the same file set it always did and existing .vfworld sector inventories keep
    // resolving.
    [[nodiscard]] inline std::string sectorFileName(uint8_t gridIndex, const SectorCoord& coord)
    {
        std::string name = "sector_";
        if (gridIndex != kPrimaryGridIndex)
            name += "g" + std::to_string(static_cast<int>(gridIndex)) + "_";
        return name + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".vfsector";
    }

    // VK-1591: UE5 World Partition "Target State" parity. Prefetching/Prefetched are APPENDED
    // (4, 5) so the integer values of the original four never move - SectorState crosses the DLL
    // boundary in GetSectorStateQuery and SectorReadiness and is mirrored into sdk/ for plugins.
    // It is never persisted (loadWorld assigns Unloaded explicitly), so there is no file-format
    // migration.
    enum class SectorState : uint8_t
    {
        Unloaded = 0,
        Loading,      // .vfsector read + parse in flight, entities WILL spawn
        Loaded,       // entities resident
        Unloading,
        Prefetching,  // VK-1591: .vfsector bytes in flight; NO entities will spawn
        Prefetched    // VK-1591: bytes resident in the service blob cache; NO entities.
                      // A Prefetched sector is NEVER dirty - nothing can assign an entity to it,
                      // and Save World skips it (WorldSectorPersistenceOps).
    };

    // VK-1591: what a ring, or an individual streaming source, wants a sector to become.
    // Ordered by increasing residency so max() composes requests across sources and min() caps
    // what a single source is allowed to ask for.
    enum class SectorTargetState : uint8_t
    {
        Unloaded = 0,
        Prefetched = 1, // bytes in memory, no entities
        Activated = 2   // entities spawned (today's only outcome)
    };

    // VK-1591: true for any state the streamer must keep tracking so its unload pass can reach it.
    // Loading/Prefetching are in flight; Loaded/Prefetched are resident.
    [[nodiscard]] inline constexpr bool isSectorTracked(SectorState state) noexcept
    {
        return state == SectorState::Loading || state == SectorState::Loaded
            || state == SectorState::Prefetching || state == SectorState::Prefetched;
    }

    struct SectorConfig
    {
        float sectorWorldSize = 128.0f;
        int32_t tilesPerSector = 4;
        bool alignedToTerrain = false;
    };

    inline SectorConfig alignSectorConfigToTerrain(float worldTileSize, int32_t tilesPerSector)
    {
        SectorConfig config;
        config.tilesPerSector = tilesPerSector;
        config.sectorWorldSize = worldTileSize * static_cast<float>(tilesPerSector);
        config.alignedToTerrain = true;
        return config;
    }

    inline bool isSectorAlignedToTerrain(const SectorConfig& config, float worldTileSize)
    {
        float expected = worldTileSize * static_cast<float>(config.tilesPerSector);
        return std::abs(config.sectorWorldSize - expected) < 0.001f;
    }

    struct StreamingSource
    {
        glm::vec3 position{0.0f};
        float radiusMultiplier = 1.0f;
        // VK-1593: world units per second, supplied by the CALLER (WorldSectorServiceImpl
        // derives it from this source's position delta and the frame's delta time). Zero means
        // no lookahead. The streamer DISCARDS this on any frame it classifies as a teleport, so
        // callers need no jump guard of their own - see SectorStreamer::update.
        glm::vec3 velocity{0.0f};
        // VK-1593: look direction; only the XZ projection is used, and it is normalized by the
        // streamer. Zero - or a straight-down camera, whose XZ projection vanishes - means omni,
        // i.e. no view bias. Left zero in edit mode: CameraPositionUpdatedNotification carries
        // no direction.
        glm::vec3 viewDir{0.0f};
        uint8_t priority = 0;
        // VK-1591: caps what this source may request of any sector it reaches. Activated (the
        // default) is exactly today's behaviour. Prefetched means "bring the bytes in, never
        // spawn" - minimap hover, speculative pre-warm.
        SectorTargetState targetState = SectorTargetState::Activated;
        // MUST be unique per source: VK-1593 keys the streamer's per-source motion tracking on
        // it. The service guarantees this - the camera is 0 and script sources start at 1.
        uint32_t id = 0;
    };

    struct SectorStreamingConfig
    {
        float loadRadius = 4.0f;    // ACTIVATE ring, in sector counts (4 = spawn sectors within 4 of camera)
        // VK-1591: outer "bytes resident, no entities" ring, in sector counts. 0 (the default)
        // means "same as loadRadius" - i.e. NO prefetch ring, byte-for-byte today's two-ring
        // behaviour. A sentinel rather than a literal 4.0 on purpose: every real config (the
        // streamer tests, the world creation wizard, every existing .vfworld) sets loadRadius
        // WITHOUT mentioning prefetchRadius, so an absolute default would silently grow a
        // prefetch ring wherever loadRadius != 4. Always read via effectivePrefetchRadius().
        float prefetchRadius = 0.0f;
        float unloadRadius = 5.0f;  // in sector counts (must exceed the OUTERMOST residency ring)
        int maxLoadsPerFrame = 1;
        // VK-1591: prefetches carry their own budget so a wide prefetch ring can never consume
        // the activation budget the player's own view depends on.
        int maxPrefetchesPerFrame = 1;
        int maxUnloadsPerFrame = 1;
        int maxEntitiesPerFrame = 8;
        int maxTerrainLoadsPerFrame = 4;    // terrain tiles loaded per frame via sector activation
        int maxTerrainUnloadsPerFrame = 4;  // terrain tiles unloaded per frame via sector deactivation
        // VK-1591: hard ceiling on resident prefetch blob bytes; 0 = unlimited. Refuses new
        // prefetches at the cap - it does not evict. Interim guard until the eviction-pool story.
        uint64_t maxPrefetchBytes = 0;

        // ---- VK-1593: predictive / view-biased prioritization + camera-jump bursts ----
        // Every field below is OFF at its default, so a .vfworld written before VK-1593 (which
        // omits all six keys) streams byte-for-byte as it did.
        //
        // Seconds of motion to extrapolate: a candidate is scored against the SMALLER of its
        // distance to the source's current position and to position + velocity * this. 0 = off.
        float lookaheadSeconds = 0.0f;
        // How hard to push sectors outside the view direction down the load order. The sort key
        // is scaled by 1 + viewBiasStrength * (1 - dot(dirToSector, viewDir)) / 2, so the factor
        // runs from 1 (dead ahead) to 1 + viewBiasStrength (dead behind). 0 = off, which is the
        // right default for a top-down RTS camera - its forward barely projects onto XZ.
        float viewBiasStrength = 0.0f;
        // A per-frame position delta beyond this many sectors is a JUMP, not motion: the frame's
        // velocity is discarded and the burst window opens. 0 is a SENTINEL meaning
        // kDefaultTeleportThresholdSectors, never "disabled" - a literal 0 threshold would make
        // every step a teleport. Read via effectiveTeleportThreshold().
        float teleportThresholdSectors = 0.0f;
        // Frames of relaxed budget after a teleport, counted from (and including) the frame the
        // teleport was detected. 0 = no burst.
        int burstFrames = 0;
        // Budgets used while the burst window is open. 0 is a SENTINEL meaning "4x the matching
        // non-burst budget" - an absolute default would stop being 4x the moment a world tunes
        // maxLoadsPerFrame / maxEntitiesPerFrame. Read via effectiveBurstLoads/Entities().
        int maxLoadsPerFrameBurst = 0;
        int maxEntitiesPerFrameBurst = 0;

        bool enableGPUObjectStreaming = true; // Use persistent GPU slots with priority-based streaming
        bool editModeStreaming = false;     // Run the streaming ring off the editor camera in edit mode

        // HLOD distance tiers (in sector counts, beyond unloadRadius)
        float hlodTier0Radius = 10.0f;
        float hlodTier1Radius = 20.0f;
        float hlodTier2Radius = 40.0f;
    };

    // VK-1591: the ONLY correct way to read the prefetch ring. Resolves the 0 sentinel and absorbs
    // a nonsensical sub-loadRadius value, so callers that never went through
    // SectorStreamer::normalizeConfig (editor sliders, WorldDefinitionSerialization, tests) still
    // get coherent semantics.
    [[nodiscard]] inline float effectivePrefetchRadius(const SectorStreamingConfig& config) noexcept
    {
        return config.prefetchRadius > config.loadRadius ? config.prefetchRadius : config.loadRadius;
    }

    // VK-1593: two sectors of travel in a single frame is not travel. Small enough that a real
    // camera pan never trips it (at 60 Hz that is 120 sectors/second), large enough to absorb a
    // frame hitch.
    inline constexpr float kDefaultTeleportThresholdSectors = 2.0f;

    // The ONLY correct way to read the three VK-1593 sentinels. Mirrors effectivePrefetchRadius:
    // callers that never went through SectorStreamer::normalizeConfig (editor sliders,
    // WorldDefinitionSerialization, tests) still get coherent semantics.
    [[nodiscard]] inline float effectiveTeleportThreshold(const SectorStreamingConfig& config) noexcept
    {
        return config.teleportThresholdSectors > 0.0f ? config.teleportThresholdSectors
                                                      : kDefaultTeleportThresholdSectors;
    }

    [[nodiscard]] inline int effectiveBurstLoads(const SectorStreamingConfig& config) noexcept
    {
        return config.maxLoadsPerFrameBurst > 0 ? config.maxLoadsPerFrameBurst
                                                : config.maxLoadsPerFrame * 4;
    }

    [[nodiscard]] inline int effectiveBurstEntities(const SectorStreamingConfig& config) noexcept
    {
        return config.maxEntitiesPerFrameBurst > 0 ? config.maxEntitiesPerFrameBurst
                                                   : config.maxEntitiesPerFrame * 4;
    }

    // VK-1595: the ring-radius coherence rules, lifted verbatim out of
    // SectorStreamer::normalizeConfig so a caller can validate a config it is NOT handing to the
    // streamer. The service needs exactly that: with a session override active, the persisted
    // config still has to be clamped even though the streamer is running the override.
    //
    // Everything that is streamer STATE rather than config - zeroing burstFramesRemaining when the
    // burst is switched off - stays behind in the member function.
    inline void normalizeStreamingConfig(SectorStreamingConfig& config) noexcept
    {
        // Resolve the 0 sentinel / a sub-loadRadius value into a concrete ring.
        if (config.prefetchRadius < config.loadRadius)
            config.prefetchRadius = config.loadRadius;

        // Hysteresis must sit outside the OUTERMOST residency ring, not just the activate ring.
        // With prefetchRadius == loadRadius this reduces exactly to the old rule.
        if (config.unloadRadius <= config.prefetchRadius)
            config.unloadRadius = config.prefetchRadius + 1.0f;
    }

    // VK-1595: the single rule for "which config is live". A session override (the editor's Debug
    // section) wins over the world's persisted config; with no override the persisted config is
    // returned BY REFERENCE and untouched, so nothing can accidentally round-trip through a copy
    // and back into worldDefinition - which is what would leak an override into the .vfworld.
    [[nodiscard]] inline const SectorStreamingConfig& effectiveStreamingConfig(
        const std::optional<SectorStreamingConfig>& sessionOverride,
        const SectorStreamingConfig& persisted) noexcept
    {
        return sessionOverride.has_value() ? *sessionOverride : persisted;
    }

} // namespace world
