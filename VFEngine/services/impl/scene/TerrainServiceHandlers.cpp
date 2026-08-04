#include "print/Log.hpp"
#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include <asset/AssetRef.hpp>
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/PaintBrushEvents.hpp"
#include "../../events/terrain/HoleBrushEvents.hpp"
#include "../../events/terrain/PaintModeEvents.hpp"
#include "../../events/terrain/HoleModeEvents.hpp"
#include "../../events/terrain/TerrainStrokeEvents.hpp"
#include "../../events/terrain/TerrainRuntimeEditEvents.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/vegetation/GrassEvents.hpp"
#include "../../events/foliage/FoliageEvents.hpp"
#include "../../events/terrain/CaveBrushEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/terrain/SplineTerrainEvents.hpp"
#include "../../events/terrain/SplineTerrainUndoEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "terrain/BrushSampler.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TileHeightSampler.hpp"
#include "terrain/WeightBrushApplicator.hpp"
#include "terrain/PaintBrushTypes.hpp"
#include "terrain/SegmentCorridor.hpp"
#include "terrain/RoadMeshBuilder.hpp"
#include "terrain/TerrainRVTBudget.hpp"
#include "vegetation/ScatterBaker.hpp"
#include "foliage/FoliageScatterBaker.hpp"
#include "../../data/VegetationUndoCommands.hpp"
#include "../../data/FoliageUndoCommands.hpp"
#include "../../events/editor/UndoRedoEvents.hpp"

namespace
{
    // Assign a tile's billboard set and set BOTH dirty flags (CPU-save + GPU-rebuild).
    // Single choke point so no mutation can forget a flag (VK-1581 dirty-flag contract).
    void writeTileBillboards(terrain::TerrainTile& tile,
                             std::vector<vegetation::BillboardInstance> instances)
    {
        tile.billboardInstances = std::move(instances);
        tile.billboardInstancesDirty = true;
        tile.billboardInstancesGPUDirty = true;
    }

    // VK-1585: assign a tile's foliage set and set BOTH dirty flags (save + GPU rebuild),
    // mirroring the foliage brush handlers' dual-dirty contract.
    void writeTileFoliage(terrain::TerrainTile& tile,
                          std::vector<foliage::FoliageInstance> instances)
    {
        tile.foliageInstances = std::move(instances);
        tile.foliageInstancesDirty = true;
        tile.foliageInstancesGPUDirty = true;
    }
}

namespace services
{
    void TerrainService::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        registerTerrainCoreHandlers(dispatcher);
        registerBrushHandlers(dispatcher);
        registerVegetationBrushHandlers(dispatcher);
        registerFoliageBrushHandlers(dispatcher);
        registerCaveBrushHandlers(dispatcher);
        registerTerrainDataHandlers(dispatcher);
        registerAsyncLoadHandlers(dispatcher);

        auto token = dispatcher.subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& notification)
            {
                onEntityDeleted(notification.entity);
            });
        entityDeletedSubscription = std::make_unique<events::SubscriptionToken>(token);

        auto sceneToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                onSceneCleared();
            });
        sceneClearedSubscription = std::make_unique<events::SubscriptionToken>(sceneToken);

        // When world loads, check if terrain already exists and activate tiles for loaded sectors
        auto worldLoadedToken = dispatcher.subscribe<events::world::WorldLoadedNotification>(
            [this](const events::world::WorldLoadedNotification&)
            {
                activateTilesForLoadedSectors();
            });
        worldLoadedSub = std::make_unique<events::SubscriptionToken>(worldLoadedToken);

        // Sector-driven terrain streaming subscriptions
        auto activatedToken = dispatcher.subscribe<events::world::SectorActivatedNotification>(
            [this](const events::world::SectorActivatedNotification& notif)
            {
                onSectorActivated(notif.coord, notif.sectorConfig);
            });
        sectorActivatedSub = std::make_unique<events::SubscriptionToken>(activatedToken);

        auto deactivatedToken = dispatcher.subscribe<events::world::SectorDeactivatedNotification>(
            [this](const events::world::SectorDeactivatedNotification& notif)
            {
                onSectorDeactivated(notif.coord, notif.sectorConfig);
            });
        sectorDeactivatedSub = std::make_unique<events::SubscriptionToken>(deactivatedToken);
    }

    void TerrainService::registerTerrainCoreHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::terrain::CreateTerrainCommand>(
            [this](const events::terrain::CreateTerrainCommand& cmd)
            {
                return createTerrain(cmd.config);
            });

        dispatcher.registerCommandHandler<events::terrain::DeleteTerrainCommand>(
            [this](const events::terrain::DeleteTerrainCommand& cmd)
            {
                return deleteTerrain(cmd.terrainEntity);
            });

        dispatcher.registerCommandHandler<events::terrain::RemapTerrainEntitiesCommand>(
            [this](const events::terrain::RemapTerrainEntitiesCommand&)
            {
                remapTerrainEntities();
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainDataQuery>(
            [this](const events::terrain::GetTerrainDataQuery& query)
            {
                return getTerrainData(query.entity);
            });

        dispatcher.registerQueryHandler<events::terrain::GetActiveTerrainTileSizeQuery>(
            [this](const events::terrain::GetActiveTerrainTileSizeQuery&) -> float
            {
                if (terrainGrids.empty()) return 0.0f;
                // Assumes single terrain per scene; returns first grid's tile size
                return terrainGrids.begin()->second->getTileConfig().worldTileSize;
            });

        dispatcher.registerQueryHandler<events::terrain::GetPendingSectorTileActionCountQuery>(
            [this](const events::terrain::GetPendingSectorTileActionCountQuery&) -> uint32_t
            {
                return static_cast<uint32_t>(pendingSectorTileActions.size());
            });

        dispatcher.registerQueryHandler<events::terrain::HasTerrainComponentQuery>(
            [this](const events::terrain::HasTerrainComponentQuery& query)
            {
                return hasTerrainComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::terrain::HasTerrainTileComponentQuery>(
            [this](const events::terrain::HasTerrainTileComponentQuery& query)
            {
                return hasTerrainTileComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainTileDataQuery>(
            [this](const events::terrain::GetTerrainTileDataQuery& query)
            {
                return getTerrainTileData(query.entity);
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainGeometryQuery>(
            [this](const events::terrain::GetTerrainGeometryQuery&)
            {
                return getTerrainGeometryForNavmesh();
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainBakeGeometryQuery>(
            [this](const events::terrain::GetTerrainBakeGeometryQuery&)
            {
                return getTerrainBakeGeometry();
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainHeightfieldQuery>(
            [this](const events::terrain::GetTerrainHeightfieldQuery&)
            {
                return getTerrainHeightfield();
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainHeightAtQuery>(
            [this](const events::terrain::GetTerrainHeightAtQuery& q)
            {
                return getTerrainHeightAt(q.worldX, q.worldZ);
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainLayerWeightsAtQuery>(
            [this](const events::terrain::GetTerrainLayerWeightsAtQuery& q)
            {
                return getTerrainLayerWeightsAt(q.worldX, q.worldZ);
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainLayerWeightsBatchQuery>(
            [this](const events::terrain::GetTerrainLayerWeightsBatchQuery& q)
            {
                return getTerrainLayerWeightsBatch(q.positions);
            });

        dispatcher.registerCommandHandler<events::terrain::AddTerrainTileCommand>(
            [this](const events::terrain::AddTerrainTileCommand& cmd)
            {
                return addTile(cmd.terrainEntity, cmd.tileX, cmd.tileZ);
            });

        dispatcher.registerCommandHandler<events::terrain::RemoveTerrainTileCommand>(
            [this](const events::terrain::RemoveTerrainTileCommand& cmd)
            {
                return removeTile(cmd.terrainEntity, cmd.tileX, cmd.tileZ);
            });

        dispatcher.registerCommandHandler<events::terrain::SetTerrainStreamingEnabledCommand>(
            [this](const events::terrain::SetTerrainStreamingEnabledCommand& cmd)
            {
                if (worldModeActive) return; // terrain streaming controlled by SectorStreamer in world mode
                auto it = worldStreamers.find(cmd.terrainEntity.id);
                if (it != worldStreamers.end() && it->second)
                    it->second->setEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::terrain::SetTerrainStreamingConfigCommand>(
            [this](const events::terrain::SetTerrainStreamingConfigCommand& cmd)
            {
                if (worldModeActive) return; // terrain streaming controlled by SectorStreamer in world mode
                auto it = worldStreamers.find(cmd.terrainEntity.id);
                if (it != worldStreamers.end() && it->second)
                {
                    terrain::StreamingConfig config;
                    config.loadRadius = cmd.loadRadius;
                    config.unloadRadius = cmd.unloadRadius;
                    config.maxLoadsPerFrame = cmd.maxLoadsPerFrame;
                    config.maxUnloadsPerFrame = cmd.maxUnloadsPerFrame;
                    it->second->setConfig(config);
                }
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainStreamingConfigQuery>(
            [this](const events::terrain::GetTerrainStreamingConfigQuery& query)
            {
                auto it = worldStreamers.find(query.terrainEntity.id);
                if (it != worldStreamers.end() && it->second)
                {
                    const auto& cfg = it->second->getConfig();
                    events::terrain::StreamingConfigData data;
                    data.loadRadius = cfg.loadRadius;
                    data.unloadRadius = cfg.unloadRadius;
                    data.maxLoadsPerFrame = cfg.maxLoadsPerFrame;
                    data.maxUnloadsPerFrame = cfg.maxUnloadsPerFrame;
                    return data;
                }
                return events::terrain::StreamingConfigData{};
            });

        dispatcher.registerQueryHandler<events::terrain::IsTerrainStreamingEnabledQuery>(
            [this](const events::terrain::IsTerrainStreamingEnabledQuery& query)
            {
                auto it = worldStreamers.find(query.terrainEntity.id);
                if (it != worldStreamers.end() && it->second)
                    return it->second->isEnabled();
                return false;
            });

        dispatcher.registerCommandHandler<events::terrain::LoadAllTilesCommand>(
            [this](const events::terrain::LoadAllTilesCommand& cmd)
            {
                loadAllTiles(cmd.terrainEntity);
            });

    }

    void TerrainService::registerBrushHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::brush::ApplyBrushCommand>(
            [this](const events::brush::ApplyBrushCommand& cmd)
            {
                applyBrush(cmd.worldPosition, cmd.deltaTime, cmd.invert, cmd.isFirstApplication);
            });

        dispatcher.registerCommandHandler<events::paintBrush::ApplyPaintBrushCommand>(
            [this](const events::paintBrush::ApplyPaintBrushCommand& cmd)
            {
                applyPaintBrush(cmd.worldPosition, cmd.deltaTime, cmd.invert, cmd.isFirstApplication);
            });

        dispatcher.registerCommandHandler<events::holeBrush::ApplyHoleBrushCommand>(
            [this](const events::holeBrush::ApplyHoleBrushCommand& cmd)
            {
                applyHoleBrush(cmd.worldPosition, cmd.erase, cmd.isFirstApplication);
            });

        // VK-1615: closes the open sculpt/paint/hole/mask stroke and pushes its undo entry.
        dispatcher.registerCommandHandler<events::terrain::FinalizeTerrainStrokeCommand>(
            [this](const events::terrain::FinalizeTerrainStrokeCommand&)
            {
                finalizeTerrainStroke();
            });

        // Undo/redo restore of a terrain stroke (heights / weights / hole masks).
        dispatcher.registerCommandHandler<events::terrain::RestoreStrokeStateCommand>(
            [this](const events::terrain::RestoreStrokeStateCommand& cmd)
            {
                restoreStrokeState(cmd.entityId, cmd.tiles);
            });

        // Undo/redo restore of a wetness/snow surface-mask stroke.
        dispatcher.registerCommandHandler<events::terrain::RestoreSurfaceMaskRegionCommand>(
            [this](const events::terrain::RestoreSurfaceMaskRegionCommand& cmd)
            {
                restoreSurfaceMaskRegion(cmd);
            });

        // VK-1624 runtime script edits. Registered here alongside the brushes because they are the
        // same kind of operation, but they share no handler with them: the three Apply*Commands
        // above resolve their target and parameters from editor tool-mode services that the Runtime
        // never constructs, so a script cannot go through them.
        dispatcher.registerCommandHandler<events::terrainEdit::DeformTerrainCommand>(
            [this](const events::terrainEdit::DeformTerrainCommand& cmd)
            {
                return deformTerrainRuntime(cmd);
            });

        dispatcher.registerCommandHandler<events::terrainEdit::PaintTerrainLayerCommand>(
            [this](const events::terrainEdit::PaintTerrainLayerCommand& cmd)
            {
                return paintTerrainLayerRuntime(cmd);
            });

        dispatcher.registerCommandHandler<events::terrainEdit::SetTerrainHolesCommand>(
            [this](const events::terrainEdit::SetTerrainHolesCommand& cmd)
            {
                return setTerrainHolesRuntime(cmd);
            });

        dispatcher.registerCommandHandler<events::terrainEdit::BeginTerrainEditBatchCommand>(
            [this](const events::terrainEdit::BeginTerrainEditBatchCommand&)
            {
                beginRuntimeTerrainEditBatch();
            });

        dispatcher.registerCommandHandler<events::terrainEdit::FlushTerrainEditsCommand>(
            [this](const events::terrainEdit::FlushTerrainEditsCommand&)
            {
                return flushRuntimeTerrainEdits();
            });

        // Leaving paint or hole mode mid-drag must not silently drop the stroke's undo
        // entry (the bug the cave brush had). Sculpt's equivalent is folded into the
        // existing SculptModeChangedNotification subscription below.
        dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                if (!n.isActive)
                    finalizeTerrainStroke();
            });

        dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                if (!n.isActive)
                    finalizeTerrainStroke();
            });

        dispatcher.registerCommandHandler<events::brush::ResetRampCommand>(
            [this](const events::brush::ResetRampCommand&)
            {
                rampStartCaptured = false;
            });

        dispatcher.subscribe<events::brush::BrushTypeChangedNotification>(
            [this](const events::brush::BrushTypeChangedNotification& n)
            {
                if (n.type != terrain::BrushType::Ramp)
                    rampStartCaptured = false;
            });

        dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                if (!n.isActive)
                {
                    rampStartCaptured = false;
                    finalizeTerrainStroke(); // VK-1615: no-op when no stroke is open
                }
            });

        dispatcher.registerQueryHandler<events::brush::IsRampStartCapturedQuery>(
            [this](const events::brush::IsRampStartCapturedQuery&)
            {
                return rampStartCaptured;
            });

        dispatcher.subscribe<events::brush::StampImageChangedNotification>(
            [this](const events::brush::StampImageChangedNotification& n)
            {
                if (!brushComputeProvider)
                    return;

                if (n.loaded)
                {
                    auto stampData = events::EventDispatcher::instance().query(
                        events::brush::GetStampDataQuery{});
                    if (stampData && stampData->isValid())
                    {
                        brushComputeProvider->setStampData(
                            stampData->heights, stampData->width, stampData->height);
                    }
                }
                else
                {
                    brushComputeProvider->clearStampData();
                }
            });

        // VK-1621: build the road ribbon for an applied spline. Runs AFTER the sculpt op, so the
        // heights it samples are the flattened corridor rather than the original ground.
        dispatcher.registerQueryHandler<events::splineTerrain::BuildSplineRoadMeshQuery>(
            [this](const events::splineTerrain::BuildSplineRoadMeshQuery& q) -> terrain::RoadMeshData
            {
                terrain::RoadMeshData result;
                if (terrainGrids.empty() || q.splineSamples.size() < 2 || q.profile.columns.size() < 2)
                    return result;

                auto& grid = terrainGrids.begin()->second;
                const auto& allTiles = grid->getAllTiles();
                if (allTiles.empty())
                    return result;
                const float worldTileSize = allTiles[0]->config.worldTileSize;

                // Widest lateral reach of the profile, so the tiles we make resident are exactly
                // the ones the sampler will touch.
                float reach = 0.0f;
                for (const auto& column : q.profile.columns)
                    reach = std::max(reach, std::abs(column.offset));

                auto cacheIt = fileCaches.find(terrainGrids.begin()->first);
                auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

                std::unordered_map<terrain::TileCoord, bool, terrain::TileCoordHash> affectedSet;
                for (size_t i = 0; i + 1 < q.splineSamples.size(); ++i)
                {
                    glm::vec2 s(q.splineSamples[i].x, q.splineSamples[i].z);
                    glm::vec2 e(q.splineSamples[i + 1].x, q.splineSamples[i + 1].z);
                    for (const auto& coord :
                         terrain::BrushSampler::getAffectedTilesForSegment(s, e, reach, worldTileSize))
                        affectedSet[coord] = true;
                }

                // Unlike GetTerrainHeightAtQuery, page streamed-out tiles back in first — a road
                // crossing an unloaded tile would otherwise be conformed to height 0.
                for (const auto& [coord, unused] : affectedSet)
                {
                    terrain::TerrainTile* tile = grid->getTile(coord);
                    if (tile && fileCache && !tile->hasHeightData())
                        fileCache->ensureHeightsLoaded(*tile);
                }

                // Sampling is per road vertex — thousands of calls — so resolve the tile through
                // TerrainGrid's O(1) hash with a one-entry memo, not the linear scan over every
                // tile that TerrainService::getTerrainHeightAt does per sample
                // (TerrainService.cpp:560-567).
                const terrain::TerrainTile* memoTile = nullptr;
                terrain::TileCoord memoCoord{std::numeric_limits<int32_t>::min(),
                                             std::numeric_limits<int32_t>::min()};
                float lastValidHeight = 0.0f;
                bool haveValidHeight = false;

                const terrain::RoadHeightFn heightFn = [&](float worldX, float worldZ) -> float
                {
                    const terrain::TileCoord coord{
                        static_cast<int32_t>(std::floor(worldX / worldTileSize)),
                        static_cast<int32_t>(std::floor(worldZ / worldTileSize))};

                    if (!memoTile || coord.x != memoCoord.x || coord.z != memoCoord.z)
                    {
                        memoTile = grid->getTile(coord);
                        memoCoord = coord;
                    }

                    // Off the terrain, or a tile that would not load: hold the last good height so
                    // the ribbon runs level off the edge instead of falling to y = 0.
                    if (!memoTile || !memoTile->hasHeightData())
                        return haveValidHeight ? lastValidHeight : 0.0f;

                    const auto& config = memoTile->config;
                    const uint32_t vpt = config.getVertexCount();
                    const uint32_t quadCount = config.getQuadCount();
                    const float spacing = config.getVertexSpacing();

                    const float localX = worldX - static_cast<float>(coord.x) * worldTileSize;
                    const float localZ = worldZ - static_cast<float>(coord.z) * worldTileSize;
                    const float gx = std::clamp(localX / spacing, 0.0f, static_cast<float>(quadCount));
                    const float gz = std::clamp(localZ / spacing, 0.0f, static_cast<float>(quadCount));
                    const uint32_t ix = std::min(static_cast<uint32_t>(gx), quadCount - 1);
                    const uint32_t iz = std::min(static_cast<uint32_t>(gz), quadCount - 1);

                    const auto& heights = memoTile->heightData;
                    const size_t row0 = static_cast<size_t>(iz) * vpt;
                    const size_t row1 = static_cast<size_t>(iz + 1) * vpt;

                    // terrainQuadHeight, NOT bilinear: terrain quads split on the anti-diagonal
                    // (TerrainTileGenerator.cpp:274-286), and down that hinge bilinear is off by
                    // the full ridge amplitude, which sinks the road into every crest.
                    lastValidHeight = terrain::terrainQuadHeight(
                        heights[row0 + ix], heights[row0 + ix + 1],
                        heights[row1 + ix], heights[row1 + ix + 1],
                        gx - static_cast<float>(ix), gz - static_cast<float>(iz));
                    haveValidHeight = true;
                    return lastValidHeight;
                };

                result = terrain::buildRoadMesh(q.splineSamples, q.profile, worldTileSize, heightFn);
                if (result.valid)
                {
                    vfLogInfo("Road mesh built: {} chunk(s), {:.1f} m{}",
                              result.chunks.size(), result.totalLength,
                              result.clamped ? " (corners pinched)" : "");
                }
                return result;
            });

        // Spline terrain deformation handlers
        dispatcher.registerQueryHandler<events::splineTerrain::ApplySplineDeformCommand>(
            [this](const events::splineTerrain::ApplySplineDeformCommand& cmd) -> bool
            {
                if (terrainGrids.empty() || cmd.splineSamples.size() < 2)
                    return false;

                auto& grid = terrainGrids.begin()->second;
                float worldTileSize = 32.0f;
                const auto& allTiles = grid->getAllTiles();
                if (!allTiles.empty())
                    worldTileSize = allTiles[0]->config.worldTileSize;

                float totalHalfWidth = cmd.params.corridorWidth + cmd.params.falloffWidth;
                float halfCorridor = cmd.params.corridorWidth;

                // Find all affected tiles across the entire spline
                std::unordered_map<terrain::TileCoord, bool, terrain::TileCoordHash> affectedSet;
                for (size_t i = 0; i + 1 < cmd.splineSamples.size(); ++i)
                {
                    glm::vec2 s(cmd.splineSamples[i].x, cmd.splineSamples[i].z);
                    glm::vec2 e(cmd.splineSamples[i + 1].x, cmd.splineSamples[i + 1].z);
                    auto tiles = terrain::BrushSampler::getAffectedTilesForSegment(s, e, totalHalfWidth, worldTileSize);
                    for (const auto& c : tiles)
                        affectedSet[c] = true;
                }

                auto cacheIt = fileCaches.find(terrainGrids.begin()->first);
                auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

                std::vector<terrain::TileCoord> modifiedTiles;

                for (const auto& [coord, _] : affectedSet)
                {
                    terrain::TerrainTile* tile = grid->getTile(coord);
                    if (!tile) continue;

                    if (fileCache && !tile->hasHeightData())
                    {
                        if (!fileCache->ensureHeightsLoaded(*tile))
                            continue;
                    }
                    if (fileCache)
                        fileCache->markDirty(coord);

                    uint32_t vertCount = tile->config.getVertexCount();
                    float vertSpacing = tile->config.getVertexSpacing();
                    glm::vec2 tileOrigin(
                        static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                        static_cast<float>(tile->coord.z) * tile->config.worldTileSize);

                    bool tileModified = false;

                    // Precompute tile AABB for segment culling
                    float tileSize = tile->config.worldTileSize;
                    glm::vec2 tileMin = tileOrigin - glm::vec2(totalHalfWidth);
                    glm::vec2 tileMax = tileOrigin + glm::vec2(tileSize + totalHalfWidth);

                    // Filter segments that overlap this tile's AABB
                    std::vector<size_t> relevantSegments;
                    for (size_t i = 0; i + 1 < cmd.splineSamples.size(); ++i)
                    {
                        glm::vec2 segStart(cmd.splineSamples[i].x, cmd.splineSamples[i].z);
                        glm::vec2 segEnd(cmd.splineSamples[i + 1].x, cmd.splineSamples[i + 1].z);
                        glm::vec2 segMin = glm::min(segStart, segEnd) - glm::vec2(totalHalfWidth);
                        glm::vec2 segMax = glm::max(segStart, segEnd) + glm::vec2(totalHalfWidth);

                        if (segMax.x >= tileOrigin.x && segMin.x <= tileOrigin.x + tileSize &&
                            segMax.y >= tileOrigin.y && segMin.y <= tileOrigin.y + tileSize)
                        {
                            relevantSegments.push_back(i);
                        }
                    }

                    if (relevantSegments.empty())
                        continue;

                    for (uint32_t z = 0; z < vertCount; ++z)
                    {
                        for (uint32_t x = 0; x < vertCount; ++x)
                        {
                            glm::vec2 vertPos = tileOrigin + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * vertSpacing;

                            // Find closest point on relevant spline segments
                            float minPerpDist = std::numeric_limits<float>::max();
                            float bestTargetHeight = 0.0f;

                            for (size_t segIdx : relevantSegments)
                            {
                                glm::vec2 segStart(cmd.splineSamples[segIdx].x, cmd.splineSamples[segIdx].z);
                                glm::vec2 segEnd(cmd.splineSamples[segIdx + 1].x, cmd.splineSamples[segIdx + 1].z);
                                glm::vec2 segDir = segEnd - segStart;
                                float segLen = glm::length(segDir);
                                if (segLen < 0.001f) continue;

                                terrain::SegmentProjection projection =
                                    terrain::projectOntoSegment(vertPos, segStart, segEnd, segLen);

                                if (projection.distance < minPerpDist)
                                {
                                    minPerpDist = projection.distance;
                                    bestTargetHeight = glm::mix(
                                        cmd.splineSamples[segIdx].y,
                                        cmd.splineSamples[segIdx + 1].y, projection.t)
                                        + cmd.params.embankmentHeight;
                                }
                            }

                            if (minPerpDist > totalHalfWidth)
                                continue;

                            uint32_t idx = z * vertCount + x;
                            float currentHeight = tile->heightData[idx];

                            float blend = terrain::corridorBlend(
                                minPerpDist, halfCorridor, cmd.params.falloffWidth);

                            tile->heightData[idx] = glm::mix(currentHeight, bestTargetHeight, blend);
                            tileModified = true;
                        }
                    }

                    if (tileModified)
                    {
                        tile->isDirty = true;
                        tile->setAllLODsDirty();
                        modifiedTiles.push_back(coord);
                    }
                }

                syncBrushBoundaryHeights(grid.get(), modifiedTiles);
                return !modifiedTiles.empty();
            });

        dispatcher.registerQueryHandler<events::splineTerrain::GetSplineOriginalHeightsQuery>(
            [this](const events::splineTerrain::GetSplineOriginalHeightsQuery& query)
            {
                std::unordered_map<terrain::TileCoord, std::vector<float>, terrain::TileCoordHash> result;

                if (terrainGrids.empty())
                    return result;

                auto& grid = terrainGrids.begin()->second;
                float worldTileSize = 32.0f;
                const auto& allTiles = grid->getAllTiles();
                if (!allTiles.empty())
                    worldTileSize = allTiles[0]->config.worldTileSize;

                auto cacheIt = fileCaches.find(terrainGrids.begin()->first);
                auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

                std::unordered_map<terrain::TileCoord, bool, terrain::TileCoordHash> affectedSet;
                for (size_t i = 0; i + 1 < query.splineSamples.size(); ++i)
                {
                    glm::vec2 s(query.splineSamples[i].x, query.splineSamples[i].z);
                    glm::vec2 e(query.splineSamples[i + 1].x, query.splineSamples[i + 1].z);
                    auto tiles = terrain::BrushSampler::getAffectedTilesForSegment(s, e, query.totalHalfWidth, worldTileSize);
                    for (const auto& c : tiles)
                        affectedSet[c] = true;
                }

                for (const auto& [coord, _] : affectedSet)
                {
                    terrain::TerrainTile* tile = grid->getTile(coord);
                    if (!tile) continue;
                    if (fileCache && !tile->hasHeightData())
                        fileCache->ensureHeightsLoaded(*tile);
                    if (tile->hasHeightData())
                        result[coord] = tile->heightData;
                }

                return result;
            });

        dispatcher.registerCommandHandler<events::splineTerrain::RestoreSplineHeightsCommand>(
            [this](const events::splineTerrain::RestoreSplineHeightsCommand& cmd)
            {
                if (terrainGrids.empty())
                    return;

                auto& grid = terrainGrids.begin()->second;
                std::vector<terrain::TileCoord> modifiedTiles;

                for (const auto& [coord, heights] : cmd.originalHeights)
                {
                    terrain::TerrainTile* tile = grid->getTile(coord);
                    if (!tile || !tile->hasHeightData())
                        continue;

                    tile->heightData = heights;
                    tile->isDirty = true;
                    tile->setAllLODsDirty();
                    modifiedTiles.push_back(coord);
                }

                syncBrushBoundaryHeights(grid.get(), modifiedTiles);
            });

        // VK-1621: the weight-map mirror of the height snapshot/restore pair above, so a painted
        // spline can be reverted. Paint records the whole per-tile weight map (channels, palette
        // indices and resolution together), because WeightBrushApplicator can evict a channel and
        // renormalize the tile — the forward operation is not invertible from the params alone.
        dispatcher.registerQueryHandler<events::splineTerrain::GetSplineOriginalWeightsQuery>(
            [this](const events::splineTerrain::GetSplineOriginalWeightsQuery& query)
            {
                events::splineTerrain::SplineWeightSnapshot result;

                if (terrainGrids.empty())
                    return result;

                auto& grid = terrainGrids.begin()->second;
                float worldTileSize = 32.0f;
                const auto& allTiles = grid->getAllTiles();
                if (!allTiles.empty())
                    worldTileSize = allTiles[0]->config.worldTileSize;

                std::unordered_map<terrain::TileCoord, bool, terrain::TileCoordHash> affectedSet;
                for (size_t i = 0; i + 1 < query.splineSamples.size(); ++i)
                {
                    glm::vec2 s(query.splineSamples[i].x, query.splineSamples[i].z);
                    glm::vec2 e(query.splineSamples[i + 1].x, query.splineSamples[i + 1].z);
                    for (const auto& coord : terrain::BrushSampler::getAffectedTilesForSegment(
                             s, e, query.totalHalfWidth, worldTileSize))
                        affectedSet[coord] = true;
                }

                for (const auto& [coord, unused] : affectedSet)
                {
                    terrain::TerrainTile* tile = grid->getTile(coord);
                    if (tile && tile->hasWeightMap())
                        result[coord] = tile->weightMap;
                }

                return result;
            });

        dispatcher.registerCommandHandler<events::splineTerrain::RestoreSplineWeightsCommand>(
            [this](const events::splineTerrain::RestoreSplineWeightsCommand& cmd)
            {
                if (terrainGrids.empty())
                    return;

                auto& grid = terrainGrids.begin()->second;
                for (const auto& [coord, weights] : cmd.originalWeights)
                {
                    terrain::TerrainTile* tile = grid->getTile(coord);
                    if (!tile || !tile->hasWeightMap())
                        continue;

                    tile->weightMap = weights;
                    tile->weightMapDirty = true;
                    tile->weightMapGPUDirty = true;
                }
            });

        dispatcher.registerQueryHandler<events::splineTerrain::ApplySplinePaintCommand>(
            [this](const events::splineTerrain::ApplySplinePaintCommand& cmd) -> bool
            {
                if (terrainGrids.empty() || cmd.splineSamples.size() < 2)
                    return false;

                auto& grid = terrainGrids.begin()->second;
                float worldTileSize = 32.0f;
                const auto& allTiles = grid->getAllTiles();
                if (!allTiles.empty())
                    worldTileSize = allTiles[0]->config.worldTileSize;

                float totalHalfWidth = cmd.params.corridorWidth + cmd.params.falloffWidth;
                float halfCorridor = cmd.params.corridorWidth;

                // Find all affected tiles
                std::unordered_map<terrain::TileCoord, bool, terrain::TileCoordHash> affectedSet;
                for (size_t i = 0; i + 1 < cmd.splineSamples.size(); ++i)
                {
                    glm::vec2 s(cmd.splineSamples[i].x, cmd.splineSamples[i].z);
                    glm::vec2 e(cmd.splineSamples[i + 1].x, cmd.splineSamples[i + 1].z);
                    auto tiles = terrain::BrushSampler::getAffectedTilesForSegment(s, e, totalHalfWidth, worldTileSize);
                    for (const auto& c : tiles)
                        affectedSet[c] = true;
                }

                auto cacheIt = fileCaches.find(terrainGrids.begin()->first);
                auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;
                bool anyModified = false;

                for (const auto& [coord, _] : affectedSet)
                {
                    terrain::TerrainTile* tile = grid->getTile(coord);
                    if (!tile) continue;

                    if (!tile->hasWeightMap())
                        continue;

                    if (fileCache && !tile->hasHeightData())
                        fileCache->ensureHeightsLoaded(*tile);
                    if (fileCache)
                        fileCache->markDirty(coord);

                    uint32_t resolution = tile->weightMap.resolution;
                    float vertSpacing = tile->config.worldTileSize / static_cast<float>(resolution - 1);
                    glm::vec2 tileOrigin(
                        static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                        static_cast<float>(tile->coord.z) * tile->config.worldTileSize);

                    // Find or assign channel for the paint layer
                    uint8_t targetChannel = 0xFF;
                    for (uint8_t ch = 0; ch < 8; ++ch)
                    {
                        if (tile->weightMap.layerIndices[ch] == static_cast<uint8_t>(cmd.params.paintLayer))
                        {
                            targetChannel = ch;
                            break;
                        }
                    }
                    if (targetChannel == 0xFF)
                    {
                        // Find empty channel
                        for (uint8_t ch = 1; ch < 8; ++ch)
                        {
                            bool inUse = false;
                            for (uint32_t i = 0; i < resolution * resolution; ++i)
                            {
                                if (tile->weightMap.getWeight(ch, i % resolution, i / resolution) > 0.001f)
                                {
                                    inUse = true;
                                    break;
                                }
                            }
                            if (!inUse)
                            {
                                tile->weightMap.layerIndices[ch] = static_cast<uint8_t>(cmd.params.paintLayer);
                                targetChannel = ch;
                                break;
                            }
                        }
                        if (targetChannel == 0xFF)
                            continue; // No free channel
                    }

                    bool tileModified = false;

                    for (uint32_t z = 0; z < resolution; ++z)
                    {
                        for (uint32_t x = 0; x < resolution; ++x)
                        {
                            glm::vec2 texelPos = tileOrigin + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * vertSpacing;

                            // Find closest point on spline
                            float minPerpDist = std::numeric_limits<float>::max();

                            for (size_t i = 0; i + 1 < cmd.splineSamples.size(); ++i)
                            {
                                glm::vec2 segStart(cmd.splineSamples[i].x, cmd.splineSamples[i].z);
                                glm::vec2 segEnd(cmd.splineSamples[i + 1].x, cmd.splineSamples[i + 1].z);
                                glm::vec2 segDir = segEnd - segStart;
                                float segLen = glm::length(segDir);
                                if (segLen < 0.001f) continue;

                                terrain::SegmentProjection projection =
                                    terrain::projectOntoSegment(texelPos, segStart, segEnd, segLen);

                                if (projection.distance < minPerpDist)
                                    minPerpDist = projection.distance;
                            }

                            if (minPerpDist > totalHalfWidth)
                                continue;

                            float blend = terrain::corridorBlend(
                                minPerpDist, halfCorridor, cmd.params.falloffWidth);

                            // Paint the target channel
                            float current = tile->weightMap.getWeight(targetChannel, x, z);
                            float newVal = glm::mix(current, 1.0f, blend);
                            tile->weightMap.setWeight(targetChannel, x, z, newVal);
                            tile->weightMap.normalizeAt(x, z);
                            tileModified = true;
                        }
                    }

                    if (tileModified)
                    {
                        tile->weightMapDirty = true;
                        tile->weightMapGPUDirty = true;
                        anyModified = true;
                    }
                }

                return anyModified;
            });
    }

    void TerrainService::registerVegetationBrushHandlers(::events::EventDispatcher& dispatcher)
    {
        // Clear all billboard instances
        dispatcher.registerCommandHandler<events::vegetation::ClearAllBillboardInstancesCommand>(
            [this](const events::vegetation::ClearAllBillboardInstancesCommand&)
            {
                for (auto& [entityId, grid] : terrainGrids)
                {
                    for (auto* tile : grid->getAllTiles())
                    {
                        if (!tile) continue;
                        tile->billboardInstances.clear();
                        tile->billboardInstancesDirty = true;
                        tile->billboardInstancesGPUDirty = true;
                    }
                }
            });

        // Add billboard instances to a tile
        dispatcher.registerCommandHandler<events::vegetation::AddBillboardInstancesToTileCommand>(
            [this](const events::vegetation::AddBillboardInstancesToTileCommand& cmd)
            {
                terrain::TileCoord coord{cmd.tileX, cmd.tileZ};
                for (auto& [entityId, grid] : terrainGrids)
                {
                    auto* tile = grid->getTile(coord);
                    if (!tile) continue;
                    tile->billboardInstances.insert(tile->billboardInstances.end(),
                                                     cmd.instances.begin(), cmd.instances.end());
                    tile->billboardInstancesDirty = true;
                    tile->billboardInstancesGPUDirty = true;
                    return;
                }
            });

        // Remove billboard instances from a tile (indices sorted descending)
        dispatcher.registerCommandHandler<events::vegetation::RemoveBillboardInstancesFromTileCommand>(
            [this](const events::vegetation::RemoveBillboardInstancesFromTileCommand& cmd)
            {
                terrain::TileCoord coord{cmd.tileX, cmd.tileZ};
                for (auto& [entityId, grid] : terrainGrids)
                {
                    auto* tile = grid->getTile(coord);
                    if (!tile) continue;
                    // Swap-and-pop (indices must be sorted descending)
                    for (uint32_t idx : cmd.indicesToRemove)
                    {
                        if (idx < tile->billboardInstances.size())
                        {
                            tile->billboardInstances[idx] = tile->billboardInstances.back();
                            tile->billboardInstances.pop_back();
                        }
                    }
                    tile->billboardInstancesDirty = true;
                    tile->billboardInstancesGPUDirty = true;
                    return;
                }
            });

        // Get billboard instances for a tile
        dispatcher.registerQueryHandler<events::vegetation::GetTileBillboardInstancesQuery>(
            [this](const events::vegetation::GetTileBillboardInstancesQuery& query)
                -> std::vector<vegetation::BillboardInstance>
            {
                terrain::TileCoord coord{query.tileX, query.tileZ};
                for (auto& [entityId, grid] : terrainGrids)
                {
                    auto* tile = grid->getTile(coord);
                    if (tile) return tile->billboardInstances;
                }
                return {};
            });

        // Replace a tile's billboard instances wholesale (undo/redo snapshots)
        dispatcher.registerCommandHandler<events::vegetation::SetTileBillboardInstancesCommand>(
            [this](const events::vegetation::SetTileBillboardInstancesCommand& cmd)
            {
                terrain::TileCoord coord{cmd.tileX, cmd.tileZ};
                for (auto& [entityId, grid] : terrainGrids)
                {
                    auto* tile = grid->getTile(coord);
                    if (!tile) continue;
                    writeTileBillboards(*tile, cmd.instances);
                    return;
                }
            });

        // VK-1581: deterministic procedural scatter bake — one undoable stroke.
        // Reads each tile's height/weight arrays directly (no per-candidate queries),
        // keeps hand-painted instances, and (when replaceProcedural) regenerates the
        // procedural set idempotently from the profile + seed.
        dispatcher.registerCommandHandler<events::vegetation::GenerateVegetationScatterCommand>(
            [this](const events::vegetation::GenerateVegetationScatterCommand& cmd)
            {
                // GPU instance budget: mirror of initialGrassCapacity in
                // GPUDrivenRendererVegetation.cpp (billboards upload as GrassInstanceGPU).
                constexpr size_t MAX_BILLBOARD_INSTANCES = 512 * 1024;

                auto& bus = events::EventDispatcher::instance();

                // Appearance palette (scale/height/tint per entry) — same source the panel edits.
                std::vector<vegetation::BillboardPaletteEntry> palette;
                try {
                    palette = bus.query(events::vegetation::GetBillboardPaletteQuery{});
                } catch (...) {}

                auto tileInRegion = [&cmd](float originX, float originZ, float size) -> bool
                {
                    if (!cmd.region) return true; // nullopt = whole terrain
                    const auto& rg = *cmd.region;
                    return !(originX + size < rg.minX || originX > rg.maxX ||
                             originZ + size < rg.minZ || originZ > rg.maxZ);
                };

                // Pass 1: how many instances survive the bake terrain-wide → budget headroom.
                size_t surviving = 0;
                for (auto& [entityId, grid] : terrainGrids)
                {
                    for (auto* tile : grid->getAllTiles())
                    {
                        if (!tile) continue;
                        const float originX = static_cast<float>(tile->coord.x) * tile->config.worldTileSize;
                        const float originZ = static_cast<float>(tile->coord.z) * tile->config.worldTileSize;
                        // Only tiles Pass 2 will actually re-bake (in-region AND have height data)
                        // may drop their Procedural instances; every other tile is left untouched and
                        // must be counted as fully surviving, or budget headroom is under-reserved and
                        // total billboards can exceed MAX_BILLBOARD_INSTANCES.
                        if (tileInRegion(originX, originZ, tile->config.worldTileSize) && tile->hasHeightData())
                        {
                            for (const auto& inst : tile->billboardInstances)
                                if (!(cmd.replaceProcedural && inst.source == vegetation::InstanceSource::Procedural))
                                    ++surviving;
                        }
                        else
                        {
                            surviving += tile->billboardInstances.size();
                        }
                    }
                }
                size_t genBudget = (surviving >= MAX_BILLBOARD_INSTANCES)
                                       ? 0 : (MAX_BILLBOARD_INSTANCES - surviving);

                auto undoCmd = std::make_shared<VegetationTileSnapshotUndoCommand>("Procedural Scatter Bake");
                uint32_t placedCount = 0;
                bool budgetExceeded = false;

                // Pass 2: bake per tile.
                for (auto& [entityId, grid] : terrainGrids)
                {
                    for (auto* tile : grid->getAllTiles())
                    {
                        if (!tile || !tile->hasHeightData()) continue;

                        const float tileSize = tile->config.worldTileSize;
                        const float originX = static_cast<float>(tile->coord.x) * tileSize;
                        const float originZ = static_cast<float>(tile->coord.z) * tileSize;
                        if (!tileInRegion(originX, originZ, tileSize)) continue;

                        std::vector<vegetation::BillboardInstance> before = tile->billboardInstances;

                        // Keep hand-painted; drop procedural when regenerating.
                        std::vector<vegetation::BillboardInstance> merged;
                        merged.reserve(before.size());
                        for (const auto& inst : before)
                        {
                            if (cmd.replaceProcedural && inst.source == vegetation::InstanceSource::Procedural)
                                continue;
                            merged.push_back(inst);
                        }

                        // Direct-tile samplers (O(1), close over this tile's arrays).
                        const float* heights = tile->heightData.data();
                        const uint32_t vpt = tile->config.getVertexCount();
                        const float vertexSpacing = tile->config.getVertexSpacing();
                        const float eps = vertexSpacing * 0.5f;
                        const terrain::TileWeightMapData& wm = tile->weightMap;
                        terrain::TileLayerWeightMemo layerMemo(wm, vertexSpacing);

                        auto result = vegetation::bakeScatterForTile(
                            cmd.profile, palette, cmd.seed,
                            originX, originZ, tileSize,
                            [heights, vpt, vertexSpacing](float lx, float lz) {
                                return terrain::sampleTileHeightBilinear(heights, vpt, vertexSpacing, lx, lz);
                            },
                            [heights, vpt, vertexSpacing, eps](float lx, float lz) {
                                return terrain::sampleTileNormalCentralDiff(heights, vpt, vertexSpacing, lx, lz, eps);
                            },
                            [&layerMemo](uint8_t layer, float lx, float lz) {
                                return layerMemo.sample(layer, lx, lz);
                            },
                            // Curvature uses a full-vertexSpacing stencil (NOT the normal's 0.5*eps):
                            // bilinear height is planar within a cell, so a narrower stencil reads ~0.
                            [heights, vpt, vertexSpacing](float lx, float lz) {
                                return terrain::sampleTileCurvature(heights, vpt, vertexSpacing, lx, lz, vertexSpacing);
                            },
                            genBudget);

                        // Guard the unsigned subtraction against a baker that ever returns more than
                        // the cap it was given (would otherwise wrap to a huge budget).
                        genBudget = result.instances.size() >= genBudget
                                        ? 0 : genBudget - result.instances.size();
                        placedCount += static_cast<uint32_t>(result.instances.size());
                        if (result.budgetExceeded) budgetExceeded = true;

                        merged.insert(merged.end(), result.instances.begin(), result.instances.end());
                        writeTileBillboards(*tile, std::move(merged));

                        undoCmd->addTile(tile->coord.x, tile->coord.z,
                                         std::move(before), tile->billboardInstances);
                    }
                }

                if (undoCmd->hasChanges())
                {
                    events::undoredo::PushUndoableCommand pushCmd;
                    pushCmd.command = undoCmd;
                    bus.execute(pushCmd);
                }

                // Report placed/total + budget warning for the panel.
                uint32_t totalAfter = 0;
                for (auto& [entityId, grid] : terrainGrids)
                    for (auto* tile : grid->getAllTiles())
                        if (tile) totalAfter += static_cast<uint32_t>(tile->billboardInstances.size());

                events::vegetation::ScatterBakeCompletedNotification note;
                note.placedCount = placedCount;
                note.totalCount = totalAfter;
                note.budgetExceeded = budgetExceeded;
                bus.publish(note);
            });
    }

    void TerrainService::registerFoliageBrushHandlers(::events::EventDispatcher& dispatcher)
    {
        // Palette (owned by TerrainService; read by the graphics collector via getFoliagePalette()).
        dispatcher.registerCommandHandler<events::foliage::SetFoliagePaletteCommand>(
            [this](const events::foliage::SetFoliagePaletteCommand& cmd)
            {
                setFoliagePalette(cmd.palette);
            });

        dispatcher.registerQueryHandler<events::foliage::GetFoliagePaletteQuery>(
            [this](const events::foliage::GetFoliagePaletteQuery&)
                -> std::vector<foliage::FoliageType>
            {
                return foliagePalette;
            });

        // Clear all foliage instances across every tile.
        dispatcher.registerCommandHandler<events::foliage::ClearAllFoliageInstancesCommand>(
            [this](const events::foliage::ClearAllFoliageInstancesCommand&)
            {
                for (auto& [entityId, grid] : terrainGrids)
                {
                    for (auto* tile : grid->getAllTiles())
                    {
                        if (!tile) continue;
                        tile->foliageInstances.clear();
                        tile->foliageInstancesDirty = true;
                        tile->foliageInstancesGPUDirty = true;
                    }
                }
            });

        // Add foliage instances to a tile.
        dispatcher.registerCommandHandler<events::foliage::AddFoliageInstancesToTileCommand>(
            [this](const events::foliage::AddFoliageInstancesToTileCommand& cmd)
            {
                terrain::TileCoord coord{cmd.tileX, cmd.tileZ};
                for (auto& [entityId, grid] : terrainGrids)
                {
                    auto* tile = grid->getTile(coord);
                    if (!tile) continue;
                    tile->foliageInstances.insert(tile->foliageInstances.end(),
                                                  cmd.instances.begin(), cmd.instances.end());
                    tile->foliageInstancesDirty = true;
                    tile->foliageInstancesGPUDirty = true;
                    return;
                }
            });

        // Remove foliage instances from a tile (indices must be sorted descending).
        dispatcher.registerCommandHandler<events::foliage::RemoveFoliageInstancesFromTileCommand>(
            [this](const events::foliage::RemoveFoliageInstancesFromTileCommand& cmd)
            {
                terrain::TileCoord coord{cmd.tileX, cmd.tileZ};
                for (auto& [entityId, grid] : terrainGrids)
                {
                    auto* tile = grid->getTile(coord);
                    if (!tile) continue;
                    // Swap-and-pop (indices must be sorted descending).
                    for (uint32_t idx : cmd.indicesToRemove)
                    {
                        if (idx < tile->foliageInstances.size())
                        {
                            tile->foliageInstances[idx] = tile->foliageInstances.back();
                            tile->foliageInstances.pop_back();
                        }
                    }
                    tile->foliageInstancesDirty = true;
                    tile->foliageInstancesGPUDirty = true;
                    return;
                }
            });

        // Get foliage instances for a tile (spatial-grid rebuild + undo snapshots).
        dispatcher.registerQueryHandler<events::foliage::GetTileFoliageInstancesQuery>(
            [this](const events::foliage::GetTileFoliageInstancesQuery& query)
                -> std::vector<foliage::FoliageInstance>
            {
                terrain::TileCoord coord{query.tileX, query.tileZ};
                for (auto& [entityId, grid] : terrainGrids)
                {
                    auto* tile = grid->getTile(coord);
                    if (tile) return tile->foliageInstances;
                }
                return {};
            });

        // Replace a tile's foliage instances wholesale (undo/redo snapshots).
        dispatcher.registerCommandHandler<events::foliage::SetTileFoliageInstancesCommand>(
            [this](const events::foliage::SetTileFoliageInstancesCommand& cmd)
            {
                terrain::TileCoord coord{cmd.tileX, cmd.tileZ};
                for (auto& [entityId, grid] : terrainGrids)
                {
                    auto* tile = grid->getTile(coord);
                    if (!tile) continue;
                    tile->foliageInstances = cmd.instances;
                    tile->foliageInstancesDirty = true;
                    tile->foliageInstancesGPUDirty = true;
                    return;
                }
            });

        // VK-1585: foliage scatter profile (lives on TerrainService; persisted in foliage_scatter.json).
        dispatcher.registerCommandHandler<events::foliage::SetFoliageScatterProfileCommand>(
            [this](const events::foliage::SetFoliageScatterProfileCommand& cmd)
            {
                setFoliageScatterProfile(cmd.profile);
            });

        dispatcher.registerQueryHandler<events::foliage::GetFoliageScatterProfileQuery>(
            [this](const events::foliage::GetFoliageScatterProfileQuery&) -> vegetation::ScatterProfile
            {
                return foliageScatterProfile;
            });

        // VK-1585: deterministic procedural MESH scatter bake — one undoable stroke. Mirrors the
        // billboard GenerateVegetationScatterCommand handler but emits FoliageInstances (reusing
        // the same rule evaluator) and distinguishes procedural vs hand-painted by the Procedural
        // flag bit (foliage has no source field). Regenerate keeps hand-painted foliage.
        dispatcher.registerCommandHandler<events::foliage::GenerateFoliageScatterCommand>(
            [this](const events::foliage::GenerateFoliageScatterCommand& cmd)
            {
                // GPU instance budget: mirror of MAX_GPU_INSTANCES (GPUDrivenTypes.hpp) — the
                // instance-transform SSBO ceiling shared by foliage/vegetation/mesh.
                constexpr size_t MAX_FOLIAGE_INSTANCES = 262144;

                auto& bus = events::EventDispatcher::instance();

                auto tileInRegion = [&cmd](float originX, float originZ, float size) -> bool
                {
                    if (!cmd.region) return true; // nullopt = whole terrain
                    const auto& rg = *cmd.region;
                    return !(originX + size < rg.minX || originX > rg.maxX ||
                             originZ + size < rg.minZ || originZ > rg.maxZ);
                };

                // Pass 1: surviving instances terrain-wide → budget headroom.
                size_t surviving = 0;
                for (auto& [entityId, grid] : terrainGrids)
                {
                    for (auto* tile : grid->getAllTiles())
                    {
                        if (!tile) continue;
                        const float originX = static_cast<float>(tile->coord.x) * tile->config.worldTileSize;
                        const float originZ = static_cast<float>(tile->coord.z) * tile->config.worldTileSize;
                        // Only tiles Pass 2 will actually re-bake (in-region AND have height data)
                        // may drop their Procedural instances; every other tile is left untouched and
                        // must be counted as fully surviving, or budget headroom is under-reserved and
                        // total foliage can exceed MAX_FOLIAGE_INSTANCES.
                        if (tileInRegion(originX, originZ, tile->config.worldTileSize) && tile->hasHeightData())
                        {
                            for (const auto& inst : tile->foliageInstances)
                                if (!(cmd.replaceProcedural && (inst.flags & foliage::FoliageInstanceFlags::Procedural)))
                                    ++surviving;
                        }
                        else
                        {
                            surviving += tile->foliageInstances.size();
                        }
                    }
                }
                size_t genBudget = (surviving >= MAX_FOLIAGE_INSTANCES)
                                       ? 0 : (MAX_FOLIAGE_INSTANCES - surviving);

                auto undoCmd = std::make_shared<FoliageTileSnapshotUndoCommand>("Procedural Foliage Scatter Bake");
                uint32_t placedCount = 0;
                bool budgetExceeded = false;

                // Pass 2: bake per tile.
                for (auto& [entityId, grid] : terrainGrids)
                {
                    for (auto* tile : grid->getAllTiles())
                    {
                        if (!tile || !tile->hasHeightData()) continue;

                        const float tileSize = tile->config.worldTileSize;
                        const float originX = static_cast<float>(tile->coord.x) * tileSize;
                        const float originZ = static_cast<float>(tile->coord.z) * tileSize;
                        if (!tileInRegion(originX, originZ, tileSize)) continue;

                        std::vector<foliage::FoliageInstance> before = tile->foliageInstances;

                        // Keep hand-painted; drop procedural when regenerating.
                        std::vector<foliage::FoliageInstance> merged;
                        merged.reserve(before.size());
                        for (const auto& inst : before)
                        {
                            if (cmd.replaceProcedural && (inst.flags & foliage::FoliageInstanceFlags::Procedural))
                                continue;
                            merged.push_back(inst);
                        }

                        const float* heights = tile->heightData.data();
                        const uint32_t vpt = tile->config.getVertexCount();
                        const float vertexSpacing = tile->config.getVertexSpacing();
                        const float eps = vertexSpacing * 0.5f;
                        const terrain::TileWeightMapData& wm = tile->weightMap;
                        terrain::TileLayerWeightMemo layerMemo(wm, vertexSpacing);

                        auto result = foliage::bakeFoliageScatterForTile(
                            cmd.profile, foliagePalette, cmd.seed,
                            originX, originZ, tileSize,
                            [heights, vpt, vertexSpacing](float lx, float lz) {
                                return terrain::sampleTileHeightBilinear(heights, vpt, vertexSpacing, lx, lz);
                            },
                            [heights, vpt, vertexSpacing, eps](float lx, float lz) {
                                return terrain::sampleTileNormalCentralDiff(heights, vpt, vertexSpacing, lx, lz, eps);
                            },
                            [&layerMemo](uint8_t layer, float lx, float lz) {
                                return layerMemo.sample(layer, lx, lz);
                            },
                            [heights, vpt, vertexSpacing](float lx, float lz) {
                                return terrain::sampleTileCurvature(heights, vpt, vertexSpacing, lx, lz, vertexSpacing);
                            },
                            genBudget);

                        // Guard the unsigned subtraction against a baker that ever returns more than
                        // the cap it was given (would otherwise wrap to a huge budget).
                        genBudget = result.instances.size() >= genBudget
                                        ? 0 : genBudget - result.instances.size();
                        placedCount += static_cast<uint32_t>(result.instances.size());
                        if (result.budgetExceeded) budgetExceeded = true;

                        merged.insert(merged.end(), result.instances.begin(), result.instances.end());
                        writeTileFoliage(*tile, std::move(merged));

                        undoCmd->addTile(tile->coord.x, tile->coord.z,
                                         std::move(before), tile->foliageInstances);
                    }
                }

                if (undoCmd->hasChanges())
                {
                    events::undoredo::PushUndoableCommand pushCmd;
                    pushCmd.command = undoCmd;
                    bus.execute(pushCmd);
                }

                uint32_t totalAfter = 0;
                for (auto& [entityId, grid] : terrainGrids)
                    for (auto* tile : grid->getAllTiles())
                        if (tile) totalAfter += static_cast<uint32_t>(tile->foliageInstances.size());

                events::foliage::FoliageScatterBakeCompletedNotification note;
                note.placedCount = placedCount;
                note.totalCount = totalAfter;
                note.budgetExceeded = budgetExceeded;
                bus.publish(note);
            });
    }

    void TerrainService::registerCaveBrushHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::caveBrush::ApplyCaveBrushCommand>(
            [this](const events::caveBrush::ApplyCaveBrushCommand& cmd)
            {
                applyCaveBrush(cmd.worldPosition, cmd.deltaTime, cmd.invert, cmd.isFirstApplication);
            });

        dispatcher.registerCommandHandler<events::caveBrush::FinalizeCaveBrushCommand>(
            [this](const events::caveBrush::FinalizeCaveBrushCommand&)
            {
                finalizeCaveBrush();
            });

        // Undo/redo restore of a cave stroke (SDF + hole mask) for a set of tiles.
        dispatcher.registerCommandHandler<::events::caveBrush::RestoreCaveStateCommand>(
            [this](const ::events::caveBrush::RestoreCaveStateCommand& cmd)
            {
                restoreCaveState(cmd.entityId, cmd.tiles);
            });
    }

    void TerrainService::registerTerrainDataHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::terrain::SetTerrainMaterialPathCommand>(
            [this](const events::terrain::SetTerrainMaterialPathCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity ent = internal::fromHandle(cmd.terrainEntity);
                if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
                {
                    auto& comp = registry.get<components::TerrainComponent>(ent);
                    auto& lifecycle = resource::AssetLifecycleManager::instance();

                    // Release old terrain material
                    auto newRef = asset::AssetRef::fromPath(cmd.materialPath);
                    if (comp.terrainMaterialRef.isValid() && comp.terrainMaterialRef != newRef)
                    {
                        lifecycle.release(comp.terrainMaterialRef.getGUID());
                    }

                    comp.terrainMaterialRef = newRef;

                    // Acquire new terrain material
                    if (newRef.isValid())
                    {
                        lifecycle.acquire(newRef.getGUID(), resource::AssetType::Material);
                    }

                    syncWeightMapLayerCount(cmd.terrainEntity.id, cmd.materialPath);
                }
            });

        dispatcher.registerCommandHandler<events::terrain::SaveWeightMapsCommand>(
            [this](const events::terrain::SaveWeightMapsCommand& cmd)
            {
                return saveWeightMaps(cmd.terrainEntity.id, cmd.path);
            });

        dispatcher.registerCommandHandler<events::terrain::LoadWeightMapsCommand>(
            [this](const events::terrain::LoadWeightMapsCommand& cmd)
            {
                return loadWeightMaps(cmd.terrainEntity.id, cmd.path);
            });

        // VK-1614 surface mask lifecycle (impl in TerrainSurfaceMaskOps.cpp).
        dispatcher.registerCommandHandler<events::terrain::CreateSurfaceMaskCommand>(
            [this](const events::terrain::CreateSurfaceMaskCommand& cmd)
            {
                return createSurfaceMask(cmd.terrainEntity.id, cmd.resolution);
            });

        dispatcher.registerCommandHandler<events::terrain::LoadSurfaceMaskCommand>(
            [this](const events::terrain::LoadSurfaceMaskCommand& cmd)
            {
                return loadSurfaceMask(cmd.terrainEntity.id, cmd.path);
            });

        dispatcher.registerCommandHandler<events::terrain::SaveSurfaceMaskCommand>(
            [this](const events::terrain::SaveSurfaceMaskCommand& cmd)
            {
                return saveSurfaceMask(cmd.path);
            });

        dispatcher.registerCommandHandler<events::terrain::ClearSurfaceMaskCommand>(
            [this](const events::terrain::ClearSurfaceMaskCommand&)
            {
                clearSurfaceMask();
            });

        dispatcher.registerQueryHandler<events::terrain::HasSurfaceMaskQuery>(
            [this](const events::terrain::HasSurfaceMaskQuery&)
            {
                return surfaceMask != nullptr && surfaceMask->isValid();
            });

        dispatcher.registerCommandHandler<events::terrain::PrepareTerrainSaveCommand>(
            [this](const events::terrain::PrepareTerrainSaveCommand& cmd)
            {
                if (cmd.incremental)
                    return prepareSaveIncremental(cmd.terrainEntity.id);
                return prepareSave(cmd.terrainEntity.id);
            });

        dispatcher.registerCommandHandler<events::terrain::SaveTerrainCommand>(
            [this](const events::terrain::SaveTerrainCommand& cmd)
            {
                if (cmd.incremental)
                    return saveTerrainIncremental(cmd.terrainEntity.id, cmd.path);
                return saveTerrain(cmd.terrainEntity.id, cmd.path);
            });

        dispatcher.registerCommandHandler<events::terrain::LoadTerrainCommand>(
            [this](const events::terrain::LoadTerrainCommand& cmd)
            {
                return loadTerrain(cmd.path, cmd.terrainRef);
            });

        dispatcher.registerCommandHandler<events::terrain::SetTerrainSaveLockCommand>(
            [this](const events::terrain::SetTerrainSaveLockCommand& cmd)
            {
                saveInProgress.store(cmd.locked, std::memory_order_release);
            });

        dispatcher.registerCommandHandler<events::physics::AddTerrainColliderCommand>(
            [this](const events::physics::AddTerrainColliderCommand& cmd)
            {
                return addTerrainCollider(cmd.terrainEntity);
            });

        dispatcher.registerCommandHandler<events::physics::RemoveTerrainColliderCommand>(
            [this](const events::physics::RemoveTerrainColliderCommand& cmd)
            {
                removeTerrainCollider(cmd.terrainEntity);
            });

        dispatcher.registerQueryHandler<events::physics::HasTerrainColliderQuery>(
            [this](const events::physics::HasTerrainColliderQuery& query)
            {
                return hasTerrainCollider(query.terrainEntity);
            });

        dispatcher.registerCommandHandler<events::physics::SetPhysicsColliderStreamConfigCommand>(
            [this](const events::physics::SetPhysicsColliderStreamConfigCommand& cmd)
            {
                if (!physicsProvider) return;
                physicsProvider->setPhysicsColliderStreamConfig(
                    cmd.memoryBudgetMB, cmd.maxCreationsPerFrame,
                    cmd.lodDistance0, cmd.lodDistance1, cmd.lodDistance2);
            });

        dispatcher.registerQueryHandler<events::physics::GetPhysicsColliderStreamConfigQuery>(
            [this](const events::physics::GetPhysicsColliderStreamConfigQuery&)
            {
                events::physics::PhysicsColliderStreamConfigData data;
                if (physicsProvider)
                {
                    auto cfg = physicsProvider->getPhysicsColliderStreamConfig();
                    data.memoryBudgetMB = cfg.memoryBudgetMB;
                    data.maxCreationsPerFrame = cfg.maxCreationsPerFrame;
                    data.lodDistance0 = cfg.lodDistance0;
                    data.lodDistance1 = cfg.lodDistance1;
                    data.lodDistance2 = cfg.lodDistance2;
                }
                return data;
            });

        dispatcher.registerCommandHandler<events::terrain::SetTerrainColliderPropertiesCommand>(
            [this](const events::terrain::SetTerrainColliderPropertiesCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity ent = internal::fromHandle(cmd.entity);
                if (registry.valid(ent) && registry.all_of<components::TerrainColliderComponent>(ent))
                {
                    auto& cc = registry.get<components::TerrainColliderComponent>(ent);
                    cc.collisionLayer = cmd.collisionLayer;
                    cc.friction = cmd.friction;
                    cc.restitution = cmd.restitution;

                    // VK-1613: the component write above is only read when a body is BUILT
                    // (buildTileColliderInfo), and the editor exposes these controls exclusively while
                    // a collider already exists — so on its own it changed nothing observable. Push
                    // the values onto the live bodies too. Component stays the source of truth, so
                    // tiles that stream in later still get the same values at creation time.
                    if (physicsProvider)
                    {
                        physicsProvider->setTerrainColliderMaterial(cmd.entity, cc.friction,
                                                                    cc.restitution, cc.collisionLayer);
                    }
                }
            });
    }

    void TerrainService::registerAsyncLoadHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::terrain::BeginCreateTerrainCommand>(
            [this](const events::terrain::BeginCreateTerrainCommand& cmd) -> bool
            {
                return beginCreateTerrainAsync(cmd.config);
            });

        dispatcher.registerCommandHandler<events::terrain::PollCreateTerrainCommand>(
            [this](const events::terrain::PollCreateTerrainCommand&) -> TerrainCreationPollResult
            {
                return pollCreateTerrain();
            });

        dispatcher.registerCommandHandler<events::terrain::BeginTerrainLoadCommand>(
            [this](const events::terrain::BeginTerrainLoadCommand& cmd) -> bool
            {
                saveInProgress.store(true, std::memory_order_release);

                std::vector<uint64_t> toDelete;
                for (auto& [id, grid] : terrainGrids)
                    toDelete.push_back(id);
                for (auto id : toDelete)
                    deleteTerrain(EntityHandle{id});

                terrain::TerrainFileHeader header;
                std::vector<terrain::TileIndexEntry> index;
                uint64_t indexTableOffset = 0;

                // Settle an interrupted save before reading — see TerrainService::loadTerrain().
                if (terrain::TerrainSerializer::recoverPending(cmd.path) ==
                    terrain::TerrainRecoveryResult::Failed)
                {
                    vfLogError("TerrainService: Could not recover an interrupted save for {}", cmd.path);
                    saveInProgress.store(false, std::memory_order_release);
                    return false;
                }

                if (!terrain::TerrainSerializer::readHeader(cmd.path, header, index, &indexTableOffset))
                {
                    vfLogError("TerrainService: Failed to read terrain header from {}", cmd.path);
                    saveInProgress.store(false, std::memory_order_release);
                    return false;
                }

                EntityHandle result = finishLoadTerrain(header, index, cmd.path, indexTableOffset);
                saveInProgress.store(false, std::memory_order_release);

                if (result.id != 0)
                {
                    events::terrain::TerrainLoadedNotification notification;
                    notification.terrainEntity = result;
                    notification.path = cmd.path;
                    events::EventDispatcher::instance().publish(notification);
                }

                vfLogInfo("TerrainService: Loaded terrain from {}", cmd.path);
                return result.id != 0;
            });

        dispatcher.registerCommandHandler<events::terrain::PollTerrainLoadCommand>(
            [this](const events::terrain::PollTerrainLoadCommand&) -> std::optional<EntityHandle>
            {
                return std::nullopt;
            });
    }
}
