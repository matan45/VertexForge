#pragma once

#include "../EventTypes.hpp"
#include "world/WorldTypes.hpp"
#include "world/SectorDataLayerOps.hpp"
#include "world/SectorRepartitionTypes.hpp"
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
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorCoord coord;
        std::string filePath;

        std::string_view getName() const override { return "SaveSector"; }
    };

    struct LoadSectorCommand : ICommand<bool>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "LoadSector"; }
    };

    struct UnloadSectorCommand : ICommand<bool>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "UnloadSector"; }
    };

    struct ClearWorldCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearWorld"; }
    };

    // ============================================
    // VK-1599 - named runtime grids
    // ============================================

    // What the editor needs to list and label the world's grids, in index order.
    struct WorldGridInfo
    {
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        std::string name;
        ::world::SectorConfig sectorConfig;
        // Resident sector counts, so the UI can tell a grid that owns content from an empty one
        // before offering to remove it.
        uint32_t sectorCount = 0;
        uint32_t loadedSectorCount = 0;
        // True for grid 0: terrain, ocean, navmesh and HLOD are bound to it, and it cannot be
        // removed. Surfaced rather than inferred so the UI does not hard-code the rule.
        bool drivesWorldSystems = false;
    };

    struct GetWorldGridsQuery : IQuery<std::vector<WorldGridInfo>>
    {
        std::string_view getName() const override { return "GetWorldGrids"; }
    };

    // Appends a grid. Returns its index, or world::kMaxGrids if the world is already at the cap.
    struct AddWorldGridCommand : ICommand<uint8_t>
    {
        std::string name;
        ::world::SectorConfig sectorConfig;
        ::world::SectorStreamingConfig streamingConfig;

        std::string_view getName() const override { return "AddWorldGrid"; }
    };

    // Removes a grid. Returns the refusal message; empty means it happened. Refuses on the primary
    // grid and on any grid that still owns sector files or resident entities - the .vfsector is the
    // only copy of that payload, so it is never dropped as a side effect.
    struct RemoveWorldGridCommand : ICommand<std::string>
    {
        uint8_t gridIndex = ::world::kPrimaryGridIndex;

        std::string_view getName() const override { return "RemoveWorldGrid"; }
    };

    // Renames a grid, and/or changes its cell size. Renaming always succeeds; a cell-size change is
    // REFUSED (returns false) on a grid that already owns sector files, because it would orphan
    // every one of them - use ApplyRepartitionCommand, which migrates the files cold. The handler
    // enforces that itself rather than trusting a caller-side gate: there is no editor call site
    // for this command, so a plugin is the only thing that can reach it.
    struct SetWorldGridCommand : ICommand<bool>
    {
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        std::string name;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SetWorldGrid"; }
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
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorCoord coord;
        std::string layerName;
        std::vector<uint8_t> data;

        std::string_view getName() const override { return "SetSectorDataLayer"; }
    };

    struct RemoveSectorDataLayerCommand : ICommand<bool>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorCoord coord;
        std::string layerName;

        std::string_view getName() const override { return "RemoveSectorDataLayer"; }
    };

    struct GetSectorDataLayerQuery : IQuery<std::optional<std::vector<uint8_t>>>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorCoord coord;
        std::string layerName;

        std::string_view getName() const override { return "GetSectorDataLayer"; }
    };

    // VK-1596: everything the editor's Data Layers tab shows, in one poll.
    //
    // Deliberately returned BY VALUE, unlike GetStreamingOverlaySnapshotQuery below. That query
    // hands out borrowed pointers into service-owned buffers and is documented SINGLE-CONSUMER,
    // naming a panel in the World Sectors window as the exact hazard - so this one must not copy
    // the trick. It is affordable because the tab polls it on the window's existing 0.25s refresh
    // timer rather than per frame, and the result is bounded by (loaded sectors x layer names).
    // Do not "optimize" it into the overlay buffers later.
    //
    // Loaded sectors only. An unloaded sector's .vfsector may well carry layers, but reading them
    // would need a TLV index scan per file; the editor labels the list accordingly.
    struct GetDataLayersSummaryQuery : IQuery<::world::DataLayerInventory>
    {
        // VK-1599: ONE grid, matching the Data Layers tab's grid selector. Aggregating across grids
        // would put coords from several grids into a single `loadedSectors` list, and every write
        // the tab makes from that list targets one grid - so a coord from another grid would
        // address the wrong sector, or a sector that does not exist.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;

        std::string_view getName() const override { return "GetDataLayersSummary"; }
    };

    // Published once per layer when a streamed-in sector carries data layers
    struct SectorDataLayerLoadedNotification : INotification
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorCoord coord;
        std::string layerName;

        std::string_view getName() const override { return "SectorDataLayerLoaded"; }
    };

    // ============================================
    // Queries
    // ============================================

    struct GetSectorAtPositionQuery : IQuery<std::optional<::world::SectorCoord>>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        glm::vec3 position{0.0f};

        std::string_view getName() const override { return "GetSectorAtPosition"; }
    };

    struct GetSectorStateQuery : IQuery<::world::SectorState>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "GetSectorState"; }
    };

    struct DoesSectorExistQuery : IQuery<bool>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "DoesSectorExist"; }
    };

    struct IsWorldModeQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsWorldMode"; }
    };

    struct GetWorldStreamingStatsQuery : IQuery<::world::SectorStreamingConfig>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        std::string_view getName() const override { return "GetWorldStreamingStats"; }
    };

    // VK-1597: entities pulled out of a sector because they are not spatially loaded, counted
    // since the last successful scene save. Non-zero means the .vfscene - which is the ONLY place
    // an always-loaded entity is persisted - is out of date, and Save World will not fix that.
    struct GetAlwaysLoadedMigrationCountQuery : IQuery<uint32_t>
    {
        std::string_view getName() const override { return "GetAlwaysLoadedMigrationCount"; }
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

        // VK-1600: eviction-pool occupancy. WHOLE-WORLD figures - the pools are shared across
        // every grid, unlike the rest of this struct's per-grid sums - and reported here rather
        // than through a new query because both consumers (the Streaming Config tab and the
        // viewport overlay) already poll this one every frame.
        //
        // `poolBytes` differs from `bytes` above: it also carries the reservations for reads
        // still in flight, which is what the budget actually gates on.
        uint64_t prefetchPoolBytes = 0;
        uint64_t prefetchPoolCap = 0; // 0 = unlimited
        uint64_t hlodProxyBytes = 0;
        uint64_t hlodProxyCap = 0;    // 0 = unlimited
        uint32_t loadedSectors = 0;
        uint32_t loadedSectorCap = 0; // 0 = unlimited
        // Since the world loaded. A counter still climbing while the camera stands still is the
        // signature of a budget too small for the ring it is being asked to hold.
        uint64_t prefetchEvictions = 0;
        uint64_t hlodEvictions = 0;
        uint64_t loadedSectorEvictions = 0;
    };

    struct GetSectorPrefetchStatsQuery : IQuery<SectorPrefetchStats>
    {
        std::string_view getName() const override { return "GetSectorPrefetchStats"; }
    };

    // Live-updates the streaming configuration (streamer + HLOD streamer + world
    // definition). Persisted on the next Save World.
    struct SetStreamingConfigCommand : ICommand<>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorStreamingConfig config;

        std::string_view getName() const override { return "SetStreamingConfig"; }
    };

    struct GetSectorConfigQuery : IQuery<::world::SectorConfig>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        std::string_view getName() const override { return "GetSectorConfig"; }
    };

    struct GetLoadedSectorCoordsQuery : IQuery<std::vector<::world::SectorCoord>>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        std::string_view getName() const override { return "GetLoadedSectorCoords"; }
    };

    // Every sector in the world definition regardless of state (world bake passes)
    struct GetAllSectorCoordsQuery : IQuery<std::vector<::world::SectorCoord>>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
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
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
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
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        std::string_view getName() const override { return "GetPersistedStreamingConfig"; }
    };

    // Installs a session-only config. It is applied to the live streamers immediately and is NEVER
    // written to worldDefinition, so a .vfworld saved while it is active is byte-identical to one
    // saved without it. Retained across play/stop; cleared when the world closes.
    struct SetStreamingConfigOverrideCommand : ICommand<>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorStreamingConfig config;

        std::string_view getName() const override { return "SetStreamingConfigOverride"; }
    };

    struct ClearStreamingConfigOverrideCommand : ICommand<>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        std::string_view getName() const override { return "ClearStreamingConfigOverride"; }
    };

    // Empty when no override is installed. The value is post-normalization, so the UI shows what
    // the streamer resolved (e.g. an unloadRadius pushed out beyond the prefetch ring).
    struct GetStreamingConfigOverrideQuery : IQuery<std::optional<::world::SectorStreamingConfig>>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
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

        // VK-1599: which grid this snapshot describes, and how many the world has. The panel shows
        // one grid at a time and lets the user step through them: the buffers below are shared and
        // refilled per query, so returning every grid at once is exactly the aliasing hazard the
        // single-consumer note above forbids.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        uint8_t gridCount = 1;
        std::string gridName;

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
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        // Hard cap on the ring half-width. The Unload Radius slider reaches 48, which would
        // otherwise mean a 101x101 grid rebuilt every frame for a panel nobody could read.
        int32_t maxRadius = 16;

        std::string_view getName() const override { return "GetStreamingOverlaySnapshot"; }
    };

    // ============================================
    // VK-1598 - re-partition / cell-size migration
    // ============================================

    // Dry run. Reads every .vfsector, builds the whole plan and throws it away, returning only the
    // counters - so what the dialog shows is produced by exactly the code Apply runs, never by a
    // parallel estimate that could disagree with it.
    //
    // `summary.valid == false` means a guard refused; `summary.refusal` is the message to show.
    struct PreviewRepartitionQuery : IQuery<::world::RepartitionSummary>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "PreviewRepartition"; }
    };

    // Destructive. Runs Save World first (the migration reads the SAVED state), writes the new
    // sector set to a temp directory, then swaps - moving the outgoing set to `sectors.bak/` and
    // the outgoing .vfworld to `<world>.vfworld.bak`. Every HLOD tier is invalidated: the bakes go
    // into the backup with the sectors they were built from, and hlodCells is cleared.
    //
    // Reloads the world on success, so the caller does not have to.
    struct ApplyRepartitionCommand : ICommand<bool>
    {
        // VK-1599: which named runtime grid this targets. 0 (the primary grid) is what
        // every pre-VK-1599 caller means, so existing call sites keep working unchanged.
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "ApplyRepartition"; }
    };

    // The inverse of the creation wizard: spawns every sector's entities into the scene, closes
    // the world, and backs up the sector set and the .vfworld.
    //
    // NOT the same operation as a repartition with a null target - the flattened world exists only
    // in the scene graph afterwards, so it is unsaved until the caller issues a SaveSceneCommand.
    // WorldFlattenedNotification is the prompt to do that.
    struct ConvertWorldToFlatCommand : ICommand<bool>
    {
        std::string_view getName() const override { return "ConvertWorldToFlat"; }
    };

    // ============================================
    // Notifications
    // ============================================

    // VK-1598: published after a successful ApplyRepartition, once the world has been reloaded.
    struct WorldRepartitionedNotification : INotification
    {
        ::world::SectorConfig sectorConfig;
        ::world::RepartitionSummary summary;
        std::string backupDirectory; // the sectors.bak/ holding the outgoing set

        std::string_view getName() const override { return "WorldRepartitioned"; }
    };

    // VK-1598: published after a successful ConvertWorldToFlat. The entities are live in the scene
    // graph and NOT yet persisted anywhere - the receiver's job is to get the user to Save Scene.
    struct WorldFlattenedNotification : INotification
    {
        uint32_t entityCount = 0;
        std::string backupDirectory;

        std::string_view getName() const override { return "WorldFlattened"; }
    };

    struct WorldLoadedNotification : INotification
    {
        std::string worldPath;

        std::string_view getName() const override { return "WorldLoaded"; }
    };

    // VK-1599: every sector notification names the grid it came from, and carries a
    // `drivesWorldSystems` flag that is true for the PRIMARY grid only.
    //
    // The flag exists because terrain tiles, ocean tiles and navmesh tiles are properties of the
    // ground, not of a grid: every grid covers the same ground, so if each one drove them a
    // two-grid world would activate every terrain tile twice and unbalance the navmesh refcounts.
    // Consumers gate on the flag rather than comparing gridIndex themselves, so the rule lives in
    // one place (WorldSectorServiceImpl::beginSectorActivation) and a future "which grid drives
    // world systems" setting changes nothing downstream.
    //
    // Consumers that care about ENTITIES rather than ground - VFXPlayModeHandler,
    // RuntimeAnimatorSystem - deliberately ignore both fields and react to every grid.

    struct SectorAboutToLoadNotification : INotification
    {
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        bool drivesWorldSystems = true;
        ::world::SectorCoord coord;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorAboutToLoad"; }
    };

    struct SectorActivatedNotification : INotification
    {
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        bool drivesWorldSystems = true;
        ::world::SectorCoord coord;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorActivated"; }
    };

    struct SectorDeactivatedNotification : INotification
    {
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        bool drivesWorldSystems = true;
        ::world::SectorCoord coord;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorDeactivated"; }
    };

    struct SectorLoadedNotification : INotification
    {
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        ::world::SectorCoord coord;
        uint32_t entityCount = 0;

        std::string_view getName() const override { return "SectorLoaded"; }
    };

    struct SectorUnloadedNotification : INotification
    {
        uint8_t gridIndex = ::world::kPrimaryGridIndex;
        bool drivesWorldSystems = true;
        ::world::SectorCoord coord;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorUnloaded"; }
    };

} // namespace events::world
