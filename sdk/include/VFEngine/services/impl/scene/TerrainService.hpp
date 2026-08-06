#pragma once
#include "../../interfaces/terrain/ITerrainService.hpp"
#include "../../providers/terrain/ITerrainRenderProvider.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/HeightLayerStackView.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "terrain/TerrainTypes.hpp"
#include "foliage/FoliageTypes.hpp"
#include "vegetation/VegetationScatterTypes.hpp"
#include "math/Frustum.hpp"
#include "../../providers/terrain/ITerrainBrushComputeProvider.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"
#include "../foliage/FoliagePhysicsActivator.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "terrain/TerrainWeightMap.hpp"
#include "terrain/SurfaceMaskBrushApplicator.hpp"
#include "terrain/TerrainFileCache.hpp"
#include "terrain/TerrainWorldStreamer.hpp"
#include "world/WorldTypes.hpp"
#include <glm/glm.hpp>
#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace scene
{
    class SceneGraphSystem;
}

namespace terrain
{
    class TerrainGrid;
    class TerrainTile;
    class TerrainHeightLayerStore; // VK-1645
    struct BaseHeightBlock;
    struct CaveSDFData;
}

namespace components
{
    struct TerrainColliderDebugData;
    struct TerrainComponent;
}

namespace events::caveBrush
{
    struct CaveTileState;
}

namespace events::terrain
{
    struct StrokeTileState;
    struct RestoreSurfaceMaskRegionCommand;
}

namespace events::terrainEdit
{
    struct DeformTerrainCommand;
    struct PaintTerrainLayerCommand;
    struct SetTerrainHolesCommand;
}

namespace services
{
    class TerrainService : public ITerrainService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        std::unordered_map<uint64_t, std::unique_ptr<terrain::TerrainGrid>> terrainGrids;

        std::unique_ptr<::events::SubscriptionToken> entityDeletedSubscription;
        std::unique_ptr<::events::SubscriptionToken> sceneClearedSubscription;

        float flattenTargetHeight = 0.0f;
        bool flattenTargetCaptured = false;

        glm::vec3 rampStartPos{0.0f};
        bool rampStartCaptured = false;

        ITerrainBrushComputeProvider* brushComputeProvider = nullptr;
        IPhysicsProvider* physicsProvider = nullptr;
        FoliagePhysicsActivator foliagePhysicsActivator; // VK-1584 proximity foliage colliders
        std::atomic<bool> saveInProgress{false};
        bool distanceCullingEnabled_ = false;
        float maxTerrainDistSq_ = 0.0f;

        // VK-1573: per-scene foliage type palette (typeIndex -> mesh/material/cull), read by
        // the graphics collector via ITerrainRenderProvider::getFoliagePalette(). Populated via
        // setFoliagePalette() by authoring (VK-1575 brush / VK-1581 scatter).
        std::vector<foliage::FoliageType> foliagePalette;

        // VK-1585: procedural MESH scatter profile (reuses vegetation::ScatterProfile; its
        // paletteEntryIndex indexes foliagePalette). Persisted in the foliage_scatter.json sidecar.
        vegetation::ScatterProfile foliageScatterProfile;

        std::unordered_map<uint64_t, std::shared_ptr<terrain::TerrainFileCache>> fileCaches;
        std::unordered_map<uint64_t, std::unique_ptr<terrain::TerrainWorldStreamer>> worldStreamers;

        // VK-1614 world-anchored wetness/snow mask. ONE mask, not one per terrain entity: it is
        // world-anchored by definition, the graphics side has a single binding for it, and scenes
        // with two terrains sharing a world are not a case the renderer models today.
        // The service owns the paintable master copy; graphics keeps its own GPU copy and pulls.
        std::shared_ptr<terrain::TerrainSurfaceMaskData> surfaceMask;
        glm::vec4 surfaceMaskWorldRect{0.0f}; // AUTHORED rect, never derived from live grid bounds
        std::string surfaceMaskPath;
        uint64_t surfaceMaskOwner = 0; // terrain entity id the mask belongs to
        std::atomic<bool> surfaceMaskAssignDirty{false};
        std::atomic<bool> surfaceMaskPixelsDirty{false};

        // Cave brush undo: per-tile SDF + hole-mask captured before the current stroke
        // first modifies that tile; drained into an undo command when the stroke finalizes.
        struct CaveStrokeTileBefore
        {
            std::vector<float> sdf;
            std::vector<uint8_t> holeMask;
        };
        std::unordered_map<terrain::TileCoord, CaveStrokeTileBefore, terrain::TileCoordHash> caveStrokeBefore;
        uint64_t caveStrokeEntityId = 0;

        // VK-1615 sculpt / weight-paint / hole / ramp stroke undo. Same shape as the cave
        // block above, with two differences: a per-kind bitmask (so a sculpt stroke does
        // not claim, and therefore cannot clobber, a tile's holes) and an explicit
        // strokeActive latch. The latch is load-bearing -- syncBrushBoundaryHeights is
        // shared with the spline tool, and without it the spline handlers would accumulate
        // snapshots that the next brush stroke pushes as part of its entry.
        enum class StrokeTool : uint8_t { None, Sculpt, Ramp, Paint, Hole, SurfaceMask };

        struct StrokeTileBefore
        {
            uint8_t kinds = 0;
            std::vector<float> heightData;
            std::vector<float> baseHeights; // VK-1645, under the BaseHeights kind
            terrain::TileWeightMapData weightMap;
            std::vector<uint8_t> holeMask;
        };
        std::unordered_map<terrain::TileCoord, StrokeTileBefore, terrain::TileCoordHash> strokeBefore;
        uint64_t strokeEntityId = 0;
        StrokeTool strokeTool = StrokeTool::None;
        bool strokeActive = false;

        // VK-1616: tiles whose physics collider is owed a rebuild when the stroke closes. The
        // hydraulic brush dabs every held frame over a multi-tile region, and
        // rebuildModifiedColliders is synchronous and unbudgeted (unlike mesh regen, which
        // TerrainGrid caps at MAX_TILE_REGEN per frame), so paying it per dab is the one hitch a
        // large erosion brush would reliably produce. Deferring it leaves the collider stale for
        // the duration of the drag, which is how every DCC sculpting mode behaves; the visual mesh
        // still updates every frame. Only the hydraulic path uses this -- the other brushes keep
        // their immediate rebuild.
        std::vector<terrain::TileCoord> strokeColliderPending;
        uint64_t strokeColliderEntityId = 0;

        // VK-1645: hydraulic erosion refuses to run over tiles under a reserved height layer.
        // Latched so a held drag logs once instead of once per frame.
        bool hydraulicCoveredWarned = false;

        // VK-1624 runtime (script-driven) edits. Same shape and the same motivation as the VK-1616
        // block above -- the collider rebuild is synchronous and unbudgeted -- but it defers the two
        // seam helpers as well, because unlike the dirty flags they are NOT idempotent:
        // syncBrushBoundaryHeights averages both sides of a seam and writes both, so running it once
        // per edit walks the seam toward the running mean.
        //
        // Height and hole tiles are tracked apart because they need different seam helpers and
        // different neighbour sets (+X/+Z vs all four). Weight edits appear in neither: weight maps
        // have no seams and are not collider input.
        struct RuntimeEditTiles
        {
            std::vector<terrain::TileCoord> heightTiles;
            std::vector<terrain::TileCoord> holeTiles;
        };
        struct RuntimeEditBatch
        {
            // Keyed per terrain entity so a scene with two terrains cannot silently drop the
            // second one's edits. Batching itself is a script-level notion, hence one `open` flag
            // for the service rather than one per entity.
            std::unordered_map<uint64_t, RuntimeEditTiles> pending;
            bool open = false;      // Script called beginBatch and has not flushed yet
            uint32_t openFrames = 0; // Safety net against a script that never flushes

            [[nodiscard]] bool empty() const { return pending.empty(); }
            void clear()
            {
                pending.clear();
                open = false;
                openFrames = 0;
            }
        };
        RuntimeEditBatch runtimeEdit;

        // VK-1614 surface-mask stroke: the painted channel's plane captured once at stroke
        // start (one byte per texel), cropped to the union of the per-dab dirty rects when
        // the stroke finalizes. The transient plane is ~1 MB at the 1024^2 default; only
        // the cropped rect is retained in the undo entry.
        std::vector<uint8_t> strokeMaskBefore;
        uint32_t strokeMaskWidth = 0;
        uint32_t strokeMaskHeight = 0;
        uint32_t strokeMaskChannel = 0;
        terrain::SurfaceMaskBrushApplicator::DirtyRect strokeMaskDirty{};
        std::vector<terrain::StreamingAction> streamingActions; // persistent scratch buffer
        std::vector<std::pair<uint64_t, terrain::TileCoord>> pendingPhysicsTiles;

        // Sector-driven terrain streaming (world mode)
        bool worldModeActive = false;
        world::SectorConfig cachedSectorConfig;
        world::SectorStreamingConfig cachedStreamingConfig;
        std::unique_ptr<::events::SubscriptionToken> sectorActivatedSub;
        std::unique_ptr<::events::SubscriptionToken> sectorDeactivatedSub;
        std::unique_ptr<::events::SubscriptionToken> worldLoadedSub;

        struct PendingTileStreamAction
        {
            uint64_t terrainEntityId;
            terrain::TileCoord coord;
            bool isLoad;
        };
        std::vector<PendingTileStreamAction> pendingSectorTileActions;

        void onSectorActivated(const world::SectorCoord& coord, const world::SectorConfig& config);
        void onSectorDeactivated(const world::SectorCoord& coord, const world::SectorConfig& config);
        void processPendingSectorTileActions();
        void activateTilesForLoadedSectors();

        // Async terrain creation state
        struct PendingTerrainCreation
        {
            TerrainCreationData config;
            std::future<std::unique_ptr<terrain::TerrainGrid>> future;
            std::atomic<float> progress{0.0f};
            std::atomic<bool> done{false};
        };
        std::shared_ptr<PendingTerrainCreation> pendingCreation;

    public:
        explicit TerrainService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~TerrainService() override;

        void registerEventHandlers() override;

        EntityHandle createTerrain(const TerrainCreationData& config) override;
        bool beginCreateTerrainAsync(const TerrainCreationData& config);
        TerrainCreationPollResult pollCreateTerrain();
        bool deleteTerrain(EntityHandle terrainEntity) override;
        std::optional<TerrainData> getTerrainData(EntityHandle entity) const override;
        bool hasTerrainComponent(EntityHandle entity) const override;

        bool hasTerrainTileComponent(EntityHandle entity) const;
        std::optional<TerrainTileData> getTerrainTileData(EntityHandle entity) const;

        std::vector<terrain::TerrainTile*> getRawVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition);

        std::vector<terrain::TerrainTile*> getAllLoadedTiles();

        std::vector<terrain::TerrainTile*> queryVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition);

        bool hasActiveTerrain() const { return !terrainGrids.empty(); }
        std::string getTerrainMaterialPath() const;
        void getTerrainGridWorldBounds(glm::vec2& outMin, glm::vec2& outMax) const;

        // VK-1573: foliage palette accessors (surfaced to graphics via TerrainRenderAdapter).
        // setFoliagePalette is the authoring entry point (VK-1575 brush / VK-1581 scatter).
        const std::vector<foliage::FoliageType>& getFoliagePalette() const { return foliagePalette; }
        void setFoliagePalette(std::vector<foliage::FoliageType> palette)
        {
            foliagePalette = std::move(palette);
            // code-review #5: palette change can invalidate the physics activator's per-type collider
            // AABB cache (mesh/shape swap). The render draw cache is handled collector-side by the
            // palette signature hash (FramePreparationSystem, code-review #4).
            foliagePhysicsActivator.onPaletteChanged();
        }

        // VK-1585: foliage scatter profile accessors (parity with the palette; authored via the
        // foliage brush panel's Scatter Rules, persisted in the foliage_scatter.json sidecar).
        const vegetation::ScatterProfile& getFoliageScatterProfile() const { return foliageScatterProfile; }
        void setFoliageScatterProfile(vegetation::ScatterProfile profile) { foliageScatterProfile = std::move(profile); }

        void setDistanceCullingEnabled(bool enabled) { distanceCullingEnabled_ = enabled; }
        void setMaxDrawDistance(float distance) { maxTerrainDistSq_ = distance * distance; }

        void applyBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication);
        void applyPaintBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication);
        void applyHoleBrush(const glm::vec3& worldPosition, bool erase, bool isFirstApplication);
        void applyCaveBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication);
        void finalizeCaveBrush();

        void setBrushComputeProvider(ITerrainBrushComputeProvider* provider) { brushComputeProvider = provider; }
        void setPhysicsProvider(IPhysicsProvider* provider)
        {
            physicsProvider = provider;
            foliagePhysicsActivator.setPhysicsProvider(provider);
        }

        bool addTerrainCollider(EntityHandle terrainEntity);
        void removeTerrainCollider(EntityHandle terrainEntity);
        bool hasTerrainCollider(EntityHandle terrainEntity) const;

        bool saveWeightMaps(uint64_t terrainEntityId, const std::string& path);
        bool loadWeightMaps(uint64_t terrainEntityId, const std::string& path);

        // VK-1614 surface mask lifecycle. Implemented in TerrainSurfaceMaskOps.cpp.
        // createSurfaceMask snapshots the CURRENT terrain world bounds into the authored rect once;
        // nothing recomputes it afterwards, so grid expansion cannot slide painted content.
        bool createSurfaceMask(uint64_t terrainEntityId, uint32_t resolution);
        bool loadSurfaceMask(uint64_t terrainEntityId, const std::string& path);
        bool saveSurfaceMask(const std::string& path);
        void clearSurfaceMask();
        [[nodiscard]] const terrain::TerrainSurfaceMaskData* getSurfaceMask() const { return surfaceMask.get(); }
        [[nodiscard]] glm::vec4 getSurfaceMaskWorldRect() const { return surfaceMaskWorldRect; }
        [[nodiscard]] const std::string& getSurfaceMaskPath() const { return surfaceMaskPath; }
        [[nodiscard]] bool consumeSurfaceMaskAssignDirty() { return surfaceMaskAssignDirty.exchange(false); }
        [[nodiscard]] bool consumeSurfaceMaskPixelsDirty() { return surfaceMaskPixelsDirty.exchange(false); }

        bool prepareSave(uint64_t terrainEntityId);
        bool prepareSaveIncremental(uint64_t terrainEntityId);
        bool saveTerrain(uint64_t terrainEntityId, const std::string& path);
        bool saveTerrainIncremental(uint64_t terrainEntityId, const std::string& path);
        EntityHandle loadTerrain(const std::string& path,
                                 const asset::AssetRef& terrainRef = asset::AssetRef::invalid());

        bool addTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ);
        bool removeTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ);

        bool streamInTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ);
        bool streamOutTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ);
        void commitStreamingChanges(EntityHandle terrainEntity);
        void loadAllTiles(EntityHandle terrainEntity);
        bool ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel);
        void releaseTileRAMData(terrain::TerrainTile& tile);
        TileLoadContextResult prepareTileLoadContext(int32_t coordX, int32_t coordZ);

        ::events::terrain::TerrainGeometryResult getTerrainGeometryForNavmesh();
        ::events::terrain::TerrainBakeGeometryResult getTerrainBakeGeometry();
        ::events::terrain::TerrainHeightfieldResult getTerrainHeightfield();
        ::terrain::TerrainHeightAtResult getTerrainHeightAt(float worldX, float worldZ);
        ::terrain::TerrainLayerWeightsAtResult getTerrainLayerWeightsAt(float worldX, float worldZ);
        std::vector<::terrain::TerrainLayerWeightsAtResult> getTerrainLayerWeightsBatch(
            const std::vector<glm::vec2>& positions);

        // VK-1624 runtime script edits. Implemented in TerrainRuntimeEditOps.cpp.
        //
        // These run on the Scripts frame task, which is NOT pinned to the main thread. That is safe
        // only because they mutate resident tiles in place and never touch grid structure: Render is
        // transitively ordered after Scripts, and every insertion / removal / regeneration lives
        // inside getRawVisibleTiles on the Render path. Do not add a streamInTile call here.
        uint32_t deformTerrainRuntime(const ::events::terrainEdit::DeformTerrainCommand& cmd);
        uint32_t paintTerrainLayerRuntime(const ::events::terrainEdit::PaintTerrainLayerCommand& cmd);
        uint32_t setTerrainHolesRuntime(const ::events::terrainEdit::SetTerrainHolesCommand& cmd);
        void beginRuntimeTerrainEditBatch();
        uint32_t flushRuntimeTerrainEdits();
        // Drained once per frame from getRawVisibleTiles: welds seams, submits colliders, notifies.
        void drainRuntimeTerrainEdits(const glm::vec3& cameraPosition);
        void discardRuntimeTerrainEdits();

    private:
        void registerTerrainCoreHandlers(::events::EventDispatcher& dispatcher);
        void registerBrushHandlers(::events::EventDispatcher& dispatcher);
        void registerTerrainDataHandlers(::events::EventDispatcher& dispatcher);
        void registerAsyncLoadHandlers(::events::EventDispatcher& dispatcher);

        void createTileEntities(EntityHandle parentEntity, terrain::TerrainGrid& grid);
        void remapTerrainEntities();
        void onEntityDeleted(EntityHandle entity);
        void onSceneCleared();
        void syncWeightMapLayerCount(uint64_t terrainEntityId, const std::string& materialPath);
        EntityHandle finishLoadTerrain(terrain::TerrainFileHeader& header,
                                       std::vector<terrain::TileIndexEntry>& index,
                                       const std::string& path,
                                       uint64_t indexTableOffset = 0,
                                       const asset::AssetRef& terrainRef = asset::AssetRef::invalid());
        void loadInitialTiles(terrain::TerrainGrid& grid,
                              const terrain::TerrainFileHeader& header,
                              const std::vector<terrain::TileIndexEntry>& index);
        void initTerrainComponent(components::TerrainComponent& comp,
                                  const terrain::TerrainFileHeader& header,
                                  const std::string& path,
                                  uint32_t activeTileCount,
                                  const asset::AssetRef& terrainRef);
        void publishTerrainCreated(EntityHandle handle, const terrain::TerrainFileHeader& header);

        void createTileEntity(EntityHandle parentEntity, terrain::TerrainTile* tile, int32_t tileX, int32_t tileZ);
        TerrainTileColliderInfo buildTileColliderInfo(const terrain::TerrainTile& tile,
                                                       EntityHandle terrainEntity,
                                                       std::vector<float>& physicsHeightsOut) const;

        void rebuildModifiedColliders(EntityHandle targetEntity, terrain::TerrainGrid* grid,
                                      const std::vector<terrain::TileCoord>& modifiedTiles);
        void syncHoleBoundaries(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles);
        void syncBrushBoundaryHeights(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles);

        // --- VK-1645: authoritative base vs derived heights (TerrainBrushOps.cpp) ---

        // The one place that decides which height plane an edit owns. Returns the tile's
        // authoritative base block when a reserved height layer covers it, and the tile's own
        // heightData otherwise -- for an uncovered tile the derived plane IS the authority.
        //
        // Static and store-taking on purpose: it is the routing policy for every sculpt path, and
        // this shape lets a CPU-only test exercise it without constructing a TerrainService.
        static std::vector<float>& authoritativeHeights(terrain::TerrainHeightLayerStore& store,
                                                        terrain::TerrainTile& tile);

        // Marks `coords` and their 4-neighbour ring derived-stale, then recomposes unbudgeted.
        // Call after any edit that wrote an authoritative plane, BEFORE the seam sync, the
        // collider rebuild and the undo "after" capture -- all three read derived output.
        // No-op when nothing is covered, so an unlayered terrain pays one hash lookup per tile.
        uint32_t recomposeAfterAuthoritativeEdit(terrain::TerrainGrid* grid,
                                                 const std::vector<terrain::TileCoord>& coords);

        // True when any of `coords` is covered. Used by the paths that refuse to run on covered
        // tiles rather than produce a wrong answer (hydraulic erosion).
        static bool anyTileCovered(terrain::TerrainGrid* grid,
                                   const std::vector<terrain::TileCoord>& coords);

        // Logs once per save when reserved height layers exist but editing is locked, i.e. when
        // this save cannot re-persist them (VK-1646).
        static void warnUnpersistedHeightLayers(const terrain::TerrainGrid& grid);

        // VK-1646. Resolves the `.vfterrainlayers` sidecar for a terrain being loaded and applies
        // the documented outcome: load the bases and stack when the pair matches, otherwise leave
        // the store empty and lock layer authoring so the flattened terrain still opens.
        //
        // Runs after the header is read and before any tile is, so coverage is settled before
        // anything can sculpt.
        static void loadHeightLayerSidecar(const std::string& path,
                                           const terrain::TerrainFileHeader& header,
                                           terrain::TerrainGrid& grid);

        // Marks every tile one layer covers, plus their rings, derived-stale. Does NOT recompose:
        // callers that are about to mutate the stack must mark first, mutate, then recompose,
        // because once the record is gone there is nothing left to ask which tiles it covered.
        static void invalidateHeightLayerTiles(terrain::TerrainGrid& grid, uint64_t layerId);

        // VK-1647. Marks an explicit coord set plus each coord's ring derived-stale. Used by the
        // in-place edit, which has to invalidate the OLD affected set as well as the new one --
        // tiles the edit narrowed off keep their (sticky) coverage and owe a recompose back to
        // base + the rest of the stack.
        static void invalidateHeightLayerCoords(
            terrain::TerrainGrid& grid,
            const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash>& coords);

        // Marks exactly the tiles a reorder can change: the moved layer's affected set intersected
        // with the union of the affected sets of the layers it crossed, plus their rings.
        //
        // That intersection is exact, not a conservative superset. A tile outside the moved layer's
        // affected set never lists it among its contributors, and the crossed layers keep their
        // relative order; a tile inside it but claimed by none of the crossed layers has the moved
        // layer sliding past layers that do not appear in that tile's contributor list at all.
        // Either way composition is unchanged, so invalidating them would be pure waste on a map
        // where one road crosses another once.
        //
        // `lo`/`hi` are stack indices, inclusive, and must bracket the move.
        static void invalidateHeightLayerReorder(terrain::TerrainGrid& grid, uint64_t movedId,
                                                 size_t lo, size_t hi);

        // Fire-and-forget "the stack changed", for a list UI that would otherwise have to poll.
        // Every mutating layer handler calls it; it is the only place the notification is built.
        void publishHeightLayerStackChanged(uint64_t terrainEntityId) const;

        // High-water mark of outstanding recompose + mesh work since the last time both reached
        // zero, so GetHeightLayerRecomposeProgressQuery's fraction only ever moves forward. Reset
        // by that same handler; nothing else touches it.
        //
        // VK-1648 moved the arithmetic to services::advanceRecomposeProgress and left the state
        // here — the function is pure so a CPU-only test can drive the transitions.
        services::RecomposeProgressLatch heightLayerRecomposeLatch;

        // VK-1648. True for the duration of one stack mutation (apply, restore, remove, visibility,
        // reorder). Layer ops compose synchronously and unbudgeted, so nothing can interleave from
        // another frame — but publishHeightLayerStackChanged dispatches to its subscribers INSIDE
        // the handler, and a subscriber that mutated the stack from that callback would reenter a
        // half-finished operation. The guard turns that into a refusal instead of corruption.
        bool heightLayerOpInFlight = false;
        void applyRamp(EntityHandle targetEntity, terrain::TerrainGrid* grid,
                       const glm::vec3& startPos, const glm::vec3& endPos,
                       const terrain::BrushParams& params);
        // VK-1616. Unlike every other sculpt brush this is not a per-tile dispatch: water has to
        // cross tile seams, so it gathers one rect in global vertex space, simulates it in a single
        // dispatch chain, and scatters the result back to every owning tile slot.
        void applyHydraulicErosion(EntityHandle targetEntity, terrain::TerrainGrid* grid,
                                   std::shared_ptr<terrain::TerrainFileCache> fileCache,
                                   const glm::vec3& worldPosition, const terrain::BrushParams& params,
                                   float deltaTime, bool invert);
        void flushPendingStrokeColliders();

        // VK-1624 helpers, in TerrainRuntimeEditOps.cpp.
        // Resolves the terrain whose grid owns the tile under a world XZ position. Unlike
        // getTerrainHeightAt, which just takes terrainGrids.begin(), this is deterministic with more
        // than one terrain in the scene -- and a runtime edit that picked the wrong grid would
        // silently deform terrain the script never named.
        terrain::TerrainGrid* resolveRuntimeEditGrid(const glm::vec2& worldXZ, uint64_t& entityIdOut);
        static void addUniqueTile(std::vector<terrain::TileCoord>& tiles, const terrain::TileCoord& coord);
        // True when paletteLayer already has a channel on this tile, or a genuinely free one exists.
        // False means WeightBrushApplicator would call assignChannel, which evicts the least-used
        // channel, zeroes it and renormalizes the WHOLE tile -- a mutation far outside the brush
        // footprint that the editor only gets away with because VK-1615 snapshots the map first.
        static bool canPaintWithoutEviction(const terrain::TileWeightMapData& weightMap,
                                            uint8_t paletteLayer);

        void generateDebugWireframes(EntityHandle terrainEntity, terrain::TerrainGrid* grid);
        static bool applyHoleMaskToHeights(const terrain::TerrainTile& tile, std::vector<float>& physicsHeights);
        static bool isVertexAdjacentToHole(const terrain::TerrainTile& tile, uint32_t vx, uint32_t vz);
        static void generateTileColliderWireframe(const terrain::TerrainTile& tile,
                                                  components::TerrainColliderDebugData& out);
        static void appendCaveWireframe(const terrain::TerrainTile& tile,
                                        components::TerrainColliderDebugData& out);
        void appendCaveColliders(EntityHandle terrainEntity,
                                 const std::vector<terrain::TerrainTile*>& allTiles);

        bool saveVegetation(uint64_t terrainEntityId, const std::string& terrainPath);
        bool loadVegetation(uint64_t terrainEntityId, const std::string& terrainPath);
        static std::string getVegetationDirectory(const std::string& terrainPath);

        // VK-1575: per-tile foliage sidecars (tile_x_z.vfFoliage) + foliage_palette.json,
        // saved/loaded next to the vegetation sidecars.
        bool saveFoliage(uint64_t terrainEntityId, const std::string& terrainPath);
        bool loadFoliage(uint64_t terrainEntityId, const std::string& terrainPath);
        static std::string getFoliageDirectory(const std::string& terrainPath);

        void registerVegetationBrushHandlers(::events::EventDispatcher& dispatcher);
        void registerFoliageBrushHandlers(::events::EventDispatcher& dispatcher);

        void registerCaveBrushHandlers(::events::EventDispatcher& dispatcher);
        void syncCaveBoundaries(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles);
        void syncCaveNeighborEdge(terrain::CaveSDFData& sdf, terrain::TerrainTile& neighbor, int axis);
        void restoreCaveState(uint64_t entityId, const std::vector<::events::caveBrush::CaveTileState>& tiles);

        // VK-1615 stroke undo. Implemented in TerrainStrokeUndoOps.cpp.
        void beginStroke(uint64_t entityId, StrokeTool tool, bool isFirstApplication);
        // VK-1645: takes the grid so ONE function decides whether a Heights request snapshots the
        // tile's derived plane or its authoritative base. Every call site still passes
        // StrokeDataKind::Heights; the routing lives here and nowhere else.
        void captureStrokeTileBefore(const terrain::TerrainGrid* grid,
                                     const terrain::TerrainTile& tile, uint8_t kinds);
        void beginSurfaceMaskStroke(uint64_t entityId, uint32_t channel, bool isFirstApplication);
        void accumulateStrokeMaskDirty(const terrain::SurfaceMaskBrushApplicator::DirtyRect& rect);
        void finalizeTerrainStroke();
        void discardTerrainStroke();
        void restoreStrokeState(uint64_t entityId,
                                const std::vector<::events::terrain::StrokeTileState>& tiles);
        void restoreSurfaceMaskRegion(const ::events::terrain::RestoreSurfaceMaskRegionCommand& cmd);
        static const char* strokeLabelFor(StrokeTool tool);
        void punchCaveHolesForTile(terrain::TerrainTile& tile);
        void rebuildCaveColliders(EntityHandle entity, terrain::TerrainGrid* grid,
                                  const std::vector<terrain::TileCoord>& caveTiles);
        static CaveTileColliderInfo buildCaveTileColliderInfo(const terrain::TerrainTile& tile,
                                                              std::vector<glm::vec3>& worldPositionsOut);

    };
}
