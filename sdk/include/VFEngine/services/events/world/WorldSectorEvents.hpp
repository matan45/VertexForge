#pragma once

#include "../EventTypes.hpp"
#include "world/WorldTypes.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>

namespace events::world
{
    // ============================================
    // Commands
    // ============================================

    struct CreateWorldCommand : ICommand<bool>
    {
        std::string name;
        std::string filePath;
        ::world::SectorConfig sectorConfig;
        ::world::SectorStreamingConfig streamingConfig;

        std::string_view getName() const override { return "CreateWorld"; }
    };

    struct SaveWorldCommand : ICommand<bool>
    {
        std::string filePath;

        std::string_view getName() const override { return "SaveWorld"; }
    };

    struct LoadWorldCommand : ICommand<bool>
    {
        std::string filePath;

        std::string_view getName() const override { return "LoadWorld"; }
    };

    struct SaveSectorCommand : ICommand<bool>
    {
        ::world::SectorCoord coord;
        std::string filePath;

        std::string_view getName() const override { return "SaveSector"; }
    };

    struct LoadSectorCommand : ICommand<bool>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "LoadSector"; }
    };

    struct UnloadSectorCommand : ICommand<bool>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "UnloadSector"; }
    };

    struct ClearWorldCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearWorld"; }
    };

    struct UpdateWorldStreamingCommand : ICommand<>
    {
        glm::vec3 cameraPos{0.0f};

        std::string_view getName() const override { return "UpdateWorldStreaming"; }
    };

    // Marks the sector owning the given entity as needing save. Editor code should
    // execute this after mutating components on a sector-managed entity (transform
    // moves are tracked automatically via TransformChangedNotification).
    struct MarkEntitySectorDirtyCommand : ICommand<>
    {
        uint64_t entityUUID = 0;

        std::string_view getName() const override { return "MarkEntitySectorDirty"; }
    };

    struct RegisterStreamingSourceCommand : ICommand<uint32_t>
    {
        glm::vec3 position{0.0f};
        float radiusMultiplier = 1.0f;
        uint8_t priority = 0;
        // VK-1591: caps what this source may request. Activated (the default) keeps every existing
        // caller - including the 6-arg _native_streaming_registerWorldSource - on exactly today's
        // behaviour; Prefetched means "bring the bytes in, never spawn".
        ::world::SectorTargetState targetState = ::world::SectorTargetState::Activated;
        // Optional owning entity: the source auto-unregisters when this entity is deleted
        uint64_t ownerEntityUUID = 0;

        std::string_view getName() const override { return "RegisterStreamingSource"; }
    };

    struct UnregisterStreamingSourceCommand : ICommand<>
    {
        uint32_t sourceId = 0;

        std::string_view getName() const override { return "UnregisterStreamingSource"; }
    };

    struct UpdateStreamingSourcePositionCommand : ICommand<>
    {
        uint32_t sourceId = 0;
        glm::vec3 position{0.0f};

        std::string_view getName() const override { return "UpdateStreamingSourcePosition"; }
    };

    struct IsStreamingSourceValidQuery : IQuery<bool>
    {
        uint32_t sourceId = 0;

        std::string_view getName() const override { return "IsStreamingSourceValid"; }
    };

    // ============================================
    // Per-sector data layers (named binary payloads persisted in vfsector v3:
    // gameplay grids, fog-of-war, plugin data)
    // ============================================

    struct SetSectorDataLayerCommand : ICommand<bool>
    {
        ::world::SectorCoord coord;
        std::string layerName;
        std::vector<uint8_t> data;

        std::string_view getName() const override { return "SetSectorDataLayer"; }
    };

    struct RemoveSectorDataLayerCommand : ICommand<bool>
    {
        ::world::SectorCoord coord;
        std::string layerName;

        std::string_view getName() const override { return "RemoveSectorDataLayer"; }
    };

    struct GetSectorDataLayerQuery : IQuery<std::optional<std::vector<uint8_t>>>
    {
        ::world::SectorCoord coord;
        std::string layerName;

        std::string_view getName() const override { return "GetSectorDataLayer"; }
    };

    // Published once per layer when a streamed-in sector carries data layers
    struct SectorDataLayerLoadedNotification : INotification
    {
        ::world::SectorCoord coord;
        std::string layerName;

        std::string_view getName() const override { return "SectorDataLayerLoaded"; }
    };

    // ============================================
    // Queries
    // ============================================

    struct GetSectorAtPositionQuery : IQuery<std::optional<::world::SectorCoord>>
    {
        glm::vec3 position{0.0f};

        std::string_view getName() const override { return "GetSectorAtPosition"; }
    };

    struct GetSectorStateQuery : IQuery<::world::SectorState>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "GetSectorState"; }
    };

    struct DoesSectorExistQuery : IQuery<bool>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "DoesSectorExist"; }
    };

    struct IsWorldModeQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsWorldMode"; }
    };

    struct GetWorldStreamingStatsQuery : IQuery<::world::SectorStreamingConfig>
    {
        std::string_view getName() const override { return "GetWorldStreamingStats"; }
    };

    // VK-1591: prefetch-ring residency for the streaming overlay. `bytes` is EXACT - it is the sum
    // of the raw .vfsector byte buffers held - unlike SectorMetadata::estimatedMemory, which is the
    // on-disk header figure.
    struct SectorPrefetchStats
    {
        uint32_t prefetchedSectors = 0;  // bytes resident, no entities
        uint32_t prefetchingSectors = 0; // read in flight
        uint64_t bytes = 0;
        uint64_t byteCap = 0;            // streamingConfig.maxPrefetchBytes; 0 = unlimited
        // VK-1593: frames of camera-jump burst window left. Reported here rather than through a
        // new query because the Streaming Config tab already polls this struct every frame, and
        // a countdown is the only way to observe the burst from the editor.
        int burstFramesRemaining = 0;
    };

    struct GetSectorPrefetchStatsQuery : IQuery<SectorPrefetchStats>
    {
        std::string_view getName() const override { return "GetSectorPrefetchStats"; }
    };

    // Live-updates the streaming configuration (streamer + HLOD streamer + world
    // definition). Persisted on the next Save World.
    struct SetStreamingConfigCommand : ICommand<>
    {
        ::world::SectorStreamingConfig config;

        std::string_view getName() const override { return "SetStreamingConfig"; }
    };

    struct GetSectorConfigQuery : IQuery<::world::SectorConfig>
    {
        std::string_view getName() const override { return "GetSectorConfig"; }
    };

    struct GetLoadedSectorCoordsQuery : IQuery<std::vector<::world::SectorCoord>>
    {
        std::string_view getName() const override { return "GetLoadedSectorCoords"; }
    };

    // Every sector in the world definition regardless of state (world bake passes)
    struct GetAllSectorCoordsQuery : IQuery<std::vector<::world::SectorCoord>>
    {
        std::string_view getName() const override { return "GetAllSectorCoords"; }
    };

    // Per-sector content readiness: which parts of a Loading sector are still in
    // flight. A sector is fully ready when state == Loaded and both flags are false.
    struct SectorReadiness
    {
        ::world::SectorState state = ::world::SectorState::Unloaded;
        bool fileLoadPending = false;    // .vfsector async read in flight
        bool entitySpawnsPending = false; // deserialized entities awaiting frame-budgeted spawn
        uint32_t entityCount = 0;
    };

    struct GetSectorReadinessQuery : IQuery<SectorReadiness>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "GetSectorReadiness"; }
    };

    struct SetSectorDebugDrawCommand : ICommand<>
    {
        bool enabled = false;

        std::string_view getName() const override { return "SetSectorDebugDraw"; }
    };

    struct GetSectorDebugDrawQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "GetSectorDebugDraw"; }
    };

    // ============================================
    // VK-1595 - session streaming override, freeze, in-viewport overlay
    // ============================================

    // The world's own persisted streaming config, i.e. exactly what Save World will write.
    // GetWorldStreamingStatsQuery deliberately reports the EFFECTIVE config (what the streamer is
    // actually running, override included), so the editor's persistent sliders must seed from this
    // one instead - seeding them from the effective config would copy an active session override
    // into worldDefinition on the very next slider drag and persist it.
    struct GetPersistedStreamingConfigQuery : IQuery<::world::SectorStreamingConfig>
    {
        std::string_view getName() const override { return "GetPersistedStreamingConfig"; }
    };

    // Installs a session-only config. It is applied to the live streamers immediately and is NEVER
    // written to worldDefinition, so a .vfworld saved while it is active is byte-identical to one
    // saved without it. Retained across play/stop; cleared when the world closes.
    struct SetStreamingConfigOverrideCommand : ICommand<>
    {
        ::world::SectorStreamingConfig config;

        std::string_view getName() const override { return "SetStreamingConfigOverride"; }
    };

    struct ClearStreamingConfigOverrideCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearStreamingConfigOverride"; }
    };

    // Empty when no override is installed. The value is post-normalization, so the UI shows what
    // the streamer resolved (e.g. an unloadRadius pushed out beyond the prefetch ring).
    struct GetStreamingConfigOverrideQuery : IQuery<std::optional<::world::SectorStreamingConfig>>
    {
        std::string_view getName() const override { return "GetStreamingConfigOverride"; }
    };

    // Freeze the streamer's DECISIONS. Loads already in flight still land, queued entities still
    // spawn and HLOD crossfades still finish - only new load/prefetch/unload decisions stop.
    struct SetStreamingPausedCommand : ICommand<>
    {
        bool paused = false;

        std::string_view getName() const override { return "SetStreamingPaused"; }
    };

    // Let exactly one streaming decision pass through while paused.
    struct StepStreamingFrameCommand : ICommand<>
    {
        std::string_view getName() const override { return "StepStreamingFrame"; }
    };

    struct GetStreamingPausedQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "GetStreamingPaused"; }
    };

    // Session-only visibility of the in-viewport overlay. Mirrors the SetSectorDebugDraw pair
    // above: the toggle lives in the World Sectors window, the drawing lives in the viewport, and
    // the service is the only thing both can see.
    struct SetStreamingOverlayVisibleCommand : ICommand<>
    {
        bool visible = false;

        std::string_view getName() const override { return "SetStreamingOverlayVisible"; }
    };

    struct GetStreamingOverlayVisibleQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "GetStreamingOverlayVisible"; }
    };

    struct StreamingOverlayCell
    {
        ::world::SectorCoord coord;
        ::world::SectorState state = ::world::SectorState::Unloaded;
        bool exists = false;      // a WorldSector exists at this coord in the world definition
        bool dirty = false;       // unsaved edits - the streamer will never auto-unload it
        bool hlodVisible = false; // a Loaded/FadingIn HLOD proxy currently covers this sector
        uint8_t hlodTier = 0;     // which tier's proxy, when hlodVisible
    };

    struct StreamingOverlaySource
    {
        glm::vec3 position{0.0f};
        float radiusMultiplier = 1.0f;
        uint8_t priority = 0;
        ::world::SectorTargetState targetState = ::world::SectorTargetState::Activated;
        bool isCamera = false; // source id 0 - the camera the service always contributes
    };

    // A per-frame view of the streaming ring for the viewport overlay.
    //
    // `cells` and `sources` are BORROWED pointers into buffers the service owns and refills on
    // every query. They are valid until the next GetStreamingOverlaySnapshotQuery and must not be
    // retained. This is what keeps the overlay path free of per-frame heap traffic: the buffers are
    // cleared, not freed, so they stop allocating after the first frame.
    //
    // *** SINGLE CONSUMER ONLY. *** This is a stronger requirement than it looks, and it is not
    // enforced by the type. The refill does `clear()` + push_back, which REALLOCATES as soon as the
    // ring grows - so a second consumer in the same frame silently invalidates whichever one ran
    // first, and the symptom is a garbage read, not a crash. Today the only call site is
    // ViewPortStreamingOverlay::draw.
    //
    // If you add a second consumer (the obvious candidates: the same panel inside the World Sectors
    // window, or a second viewport), do NOT try to make the sharing work - convert this to
    // caller-owned out-params, i.e. put `std::vector<StreamingOverlayCell>*` /
    // `std::vector<StreamingOverlaySource>*` on the QUERY and return only the scalars by value.
    // That keeps the identical zero-steady-state-allocation property and deletes the aliasing
    // question outright. In-engine precedent: HLODProxyManager::collectExpiredProxies(out).
    //
    // Note this borrows a pointer OUT of the handler, which is a longer and weaker lifetime than
    // RegisterHLODMeshCommand::data, which borrows one IN for the duration of a single execute().
    //
    // Safe because Services is a StaticLib linked into the same binary as the editor, and both the
    // producer (WorldSectorServiceImpl::update, main-thread pinned) and the consumer (ImGui draw)
    // run on the main thread.
    struct StreamingOverlaySnapshot
    {
        bool valid = false; // false when there is no world open
        ::world::SectorCoord center;
        int32_t radius = 0;         // ring half-width in sectors; the grid is (2*radius+1)^2
        bool radiusClamped = false; // the ring was wider than the query's maxRadius allowed
        float sectorWorldSize = 0.0f;

        // EFFECTIVE radii, in sector counts - override included, sentinels already resolved.
        float loadRadius = 0.0f;
        float prefetchRadius = 0.0f;
        float unloadRadius = 0.0f;

        bool overrideActive = false;
        bool paused = false;
        int burstFramesRemaining = 0;

        const StreamingOverlayCell* cells = nullptr;
        uint32_t cellCount = 0; // row-major: z descending (north up), then x ascending
        const StreamingOverlaySource* sources = nullptr;
        uint32_t sourceCount = 0;
    };

    struct GetStreamingOverlaySnapshotQuery : IQuery<StreamingOverlaySnapshot>
    {
        // Hard cap on the ring half-width. The Unload Radius slider reaches 48, which would
        // otherwise mean a 101x101 grid rebuilt every frame for a panel nobody could read.
        int32_t maxRadius = 16;

        std::string_view getName() const override { return "GetStreamingOverlaySnapshot"; }
    };

    // ============================================
    // Notifications
    // ============================================

    struct WorldLoadedNotification : INotification
    {
        std::string worldPath;

        std::string_view getName() const override { return "WorldLoaded"; }
    };

    struct SectorAboutToLoadNotification : INotification
    {
        ::world::SectorCoord coord;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorAboutToLoad"; }
    };

    struct SectorActivatedNotification : INotification
    {
        ::world::SectorCoord coord;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorActivated"; }
    };

    struct SectorDeactivatedNotification : INotification
    {
        ::world::SectorCoord coord;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorDeactivated"; }
    };

    struct SectorLoadedNotification : INotification
    {
        ::world::SectorCoord coord;
        uint32_t entityCount = 0;

        std::string_view getName() const override { return "SectorLoaded"; }
    };

    struct SectorUnloadedNotification : INotification
    {
        ::world::SectorCoord coord;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorUnloaded"; }
    };

} // namespace events::world
