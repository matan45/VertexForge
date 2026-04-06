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
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/vegetation/GrassEvents.hpp"
#include "../../events/terrain/CaveBrushEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/terrain/SplineTerrainEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "terrain/BrushSampler.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/WeightBrushApplicator.hpp"
#include "terrain/PaintBrushTypes.hpp"

namespace services
{
    void TerrainService::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        registerTerrainCoreHandlers(dispatcher);
        registerBrushHandlers(dispatcher);
        registerVegetationBrushHandlers(dispatcher);
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
                applyHoleBrush(cmd.worldPosition, cmd.erase);
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
                    rampStartCaptured = false;
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

                            for (size_t idx : relevantSegments)
                            {
                                glm::vec2 segStart(cmd.splineSamples[idx].x, cmd.splineSamples[idx].z);
                                glm::vec2 segEnd(cmd.splineSamples[idx + 1].x, cmd.splineSamples[idx + 1].z);
                                glm::vec2 segDir = segEnd - segStart;
                                float segLen = glm::length(segDir);
                                if (segLen < 0.001f) continue;

                                glm::vec2 segNorm = segDir / segLen;
                                float t = glm::dot(vertPos - segStart, segNorm) / segLen;
                                t = glm::clamp(t, 0.0f, 1.0f);

                                glm::vec2 closest = segStart + segDir * t;
                                float perpDist = glm::length(vertPos - closest);

                                if (perpDist < minPerpDist)
                                {
                                    minPerpDist = perpDist;
                                    bestTargetHeight = glm::mix(
                                        cmd.splineSamples[idx].y,
                                        cmd.splineSamples[idx + 1].y, t)
                                        + cmd.params.embankmentHeight;
                                }
                            }

                            if (minPerpDist > totalHalfWidth)
                                continue;

                            uint32_t idx = z * vertCount + x;
                            float currentHeight = tile->heightData[idx];

                            float blend = 1.0f;
                            if (minPerpDist > halfCorridor && cmd.params.falloffWidth > 0.0f)
                            {
                                float falloffT = (minPerpDist - halfCorridor) / cmd.params.falloffWidth;
                                blend = 1.0f - falloffT * falloffT * (3.0f - 2.0f * falloffT);
                            }

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

                                float t = glm::dot(texelPos - segStart, segDir / segLen) / segLen;
                                t = glm::clamp(t, 0.0f, 1.0f);

                                glm::vec2 closest = segStart + segDir * t;
                                float perpDist = glm::length(texelPos - closest);

                                if (perpDist < minPerpDist)
                                    minPerpDist = perpDist;
                            }

                            if (minPerpDist > totalHalfWidth)
                                continue;

                            float blend = 1.0f;
                            if (minPerpDist > halfCorridor && cmd.params.falloffWidth > 0.0f)
                            {
                                float falloffT = (minPerpDist - halfCorridor) / cmd.params.falloffWidth;
                                blend = 1.0f - falloffT * falloffT * (3.0f - 2.0f * falloffT);
                            }

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
                return loadTerrain(cmd.path);
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
