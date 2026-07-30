#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/BrushSampler.hpp"
#include "terrain/WeightBrushApplicator.hpp"
#include "terrain/SurfaceMaskBrushApplicator.hpp"
#include "terrain/HoleBrushApplicator.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/PaintBrushEvents.hpp"
#include "../../events/terrain/HoleBrushEvents.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/vegetation/GrassEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "../../events/render/DebugDrawEvents.hpp"
#include "../../events/terrain/PaintModeEvents.hpp"
#include "../../events/terrain/HoleModeEvents.hpp"

namespace services
{
    void TerrainService::applyBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::sculpt::GetSculptTargetEntityQuery{});
        if (!targetEntity.has_value())
            return;

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
            return;

        terrain::TerrainGrid* grid = gridIt->second.get();

        auto brushType = dispatcher.query(events::brush::GetBrushTypeQuery{});
        auto brushParams = dispatcher.query(events::brush::GetBrushParamsQuery{});

        // Query stamp data dimensions for GPU params (upload handled by StampImageChangedNotification)
        std::shared_ptr<terrain::HeightmapData> stampData;
        if (brushType == terrain::BrushType::Stamp)
        {
            stampData = dispatcher.query(events::brush::GetStampDataQuery{});
        }

        if (brushType == terrain::BrushType::Flatten)
        {
            if (isFirstApplication)
            {
                flattenTargetCaptured = true;
                flattenTargetHeight = worldPosition.y;
            }
        }
        else
        {
            flattenTargetCaptured = false;
        }

        // Stamp is a one-shot operation per click
        if (brushType == terrain::BrushType::Stamp && !isFirstApplication)
            return;

        // Ramp: two-click workflow
        if (brushType == terrain::BrushType::Ramp)
        {
            if (isFirstApplication)
            {
                if (!rampStartCaptured)
                {
                    rampStartPos = worldPosition;
                    rampStartCaptured = true;
                }
                else
                {
                    applyRamp(*targetEntity, grid, rampStartPos, worldPosition, brushParams);
                    rampStartCaptured = false;
                }
            }
            else if (rampStartCaptured)
            {
                // Draw preview lines while dragging after first click
                float halfWidth = brushParams.rampWidth * 0.5f;
                glm::vec3 dir = worldPosition - rampStartPos;
                glm::vec2 dir2D(dir.x, dir.z);
                float len = glm::length(dir2D);
                if (len > 0.01f)
                {
                    glm::vec2 perp = glm::normalize(glm::vec2(-dir2D.y, dir2D.x)) * halfWidth;
                    glm::vec3 perpOffset(perp.x, 0.0f, perp.y);

                    events::debugdraw::DrawLineCommand centerLine;
                    centerLine.start = rampStartPos;
                    centerLine.end = worldPosition;
                    centerLine.color = glm::vec4(0.2f, 0.6f, 1.0f, 1.0f);
                    dispatcher.execute(centerLine);

                    events::debugdraw::DrawLineCommand leftLine;
                    leftLine.start = rampStartPos + perpOffset;
                    leftLine.end = worldPosition + perpOffset;
                    leftLine.color = glm::vec4(0.2f, 0.6f, 1.0f, 0.5f);
                    dispatcher.execute(leftLine);

                    events::debugdraw::DrawLineCommand rightLine;
                    rightLine.start = rampStartPos - perpOffset;
                    rightLine.end = worldPosition - perpOffset;
                    rightLine.color = glm::vec4(0.2f, 0.6f, 1.0f, 0.5f);
                    dispatcher.execute(rightLine);
                }
            }
            return;
        }

        glm::vec2 brushCenter(worldPosition.x, worldPosition.z);
        float worldTileSize = 32.0f;
        const auto& allTiles = grid->getAllTiles();
        if (!allTiles.empty())
        {
            worldTileSize = allTiles[0]->config.worldTileSize;
        }
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            brushCenter, brushParams.radius, worldTileSize);

        auto cacheIt = fileCaches.find(targetEntity->id);
        auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        std::vector<terrain::TileCoord> modifiedTiles;
        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
            {
                if (fileCache && fileCache->hasCoord(coord))
                {
                    streamInTile(*targetEntity, coord.x, coord.z);
                    tile = grid->getTile(coord);
                }
                if (!tile)
                    continue;
            }

            if (!brushComputeProvider)
                continue;

            if (fileCache && !tile->hasHeightData())
            {
                if (!fileCache->ensureHeightsLoaded(*tile))
                    continue;
            }
            if (fileCache)
                fileCache->markDirty(coord);

            terrain::BrushGPUParams gpuParams;
            gpuParams.brushCenter = brushCenter;
            gpuParams.tileWorldOrigin = glm::vec2(
                static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                static_cast<float>(tile->coord.z) * tile->config.worldTileSize);
            gpuParams.brushRadius = brushParams.radius;
            gpuParams.brushStrength = brushParams.strength;
            gpuParams.vertexSpacing = tile->config.getVertexSpacing();
            gpuParams.verticesPerSide = tile->config.getVertexCount();
            gpuParams.falloff = brushParams.falloff;
            gpuParams.shape = brushParams.shape;
            gpuParams.brushType = brushType;
            gpuParams.deltaTime = deltaTime;
            gpuParams.targetHeight = flattenTargetHeight;
            gpuParams.minHeight = tile->config.minHeight;
            gpuParams.maxHeight = tile->config.maxHeight;
            gpuParams.invert = (brushType == terrain::BrushType::Stamp)
                ? (invert != brushParams.stampSubtract)  // XOR: Shift toggles the UI mode
                : invert;
            gpuParams.stampRotation = brushParams.stampRotation;
            gpuParams.stampScale = brushParams.stampScale;
            gpuParams.talusAngle = brushParams.talusAngle;
            gpuParams.terraceStepHeight = brushParams.terraceStepHeight;
            gpuParams.terraceSharpness = brushParams.terraceSharpness;
            if (stampData && stampData->isValid())
            {
                gpuParams.stampWidth = stampData->width;
                gpuParams.stampHeight = stampData->height;
            }

            if (brushComputeProvider->applyBrushGPU(tile->heightData, gpuParams))
            {
                tile->isDirty = true;
                tile->setAllLODsDirty();
                modifiedTiles.push_back(coord);
            }
            else
            {
                vfLogError("GPU brush application failed for tile ({}, {})", coord.x, coord.z);
            }
        }

        // Synchronize boundary heights between adjacent modified tiles
        syncBrushBoundaryHeights(grid, modifiedTiles);

        events::brush::BrushAppliedNotification notification;
        notification.position = worldPosition;
        notification.type = brushType;
        dispatcher.publish(notification);

        rebuildModifiedColliders(*targetEntity, grid, modifiedTiles);
    }

    void TerrainService::applyPaintBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::paint::GetPaintTargetEntityQuery{});
        if (!targetEntity.has_value())
            return;

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
            return;

        terrain::TerrainGrid* grid = gridIt->second.get();

        auto brushParams = dispatcher.query(events::paintBrush::GetPaintBrushParamsQuery{});
        auto brushType = dispatcher.query(events::paintBrush::GetPaintBrushTypeQuery{});

        // VK-1614: a wetness/snow stroke writes one world-anchored image, not the per-tile weight
        // maps, so it short-circuits the whole tile loop below (no affected-tile query, no
        // stream-in, no palette indirection, no cross-channel renormalisation).
        if (brushParams.target != terrain::PaintTarget::Layers)
        {
            if (surfaceMask && surfaceMask->isValid())
            {
                terrain::SurfaceMaskBrushApplicator::ApplyParams maskParams;
                maskParams.brushCenter = glm::vec2(worldPosition.x, worldPosition.z);
                maskParams.maskWorldRect = surfaceMaskWorldRect;
                maskParams.brushRadius = brushParams.radius;
                maskParams.brushStrength = brushParams.strength;
                maskParams.brushOpacity = brushParams.opacity;
                maskParams.falloff = brushParams.falloff;
                maskParams.shape = brushParams.shape;
                maskParams.channel = terrain::paintTargetToMaskChannel(brushParams.target);
                maskParams.deltaTime = deltaTime;
                maskParams.invert = invert;

                const auto dirtyRect =
                    terrain::SurfaceMaskBrushApplicator::apply(*surfaceMask, maskParams);
                if (!dirtyRect.isEmpty())
                {
                    // Whole-image re-upload: the mask is one texture, and a partial vk::BufferImageCopy
                    // would need its own staging slice per stroke for a 4 MB image that is only
                    // touched while a human is dragging a brush. Revisit if profiling says otherwise.
                    surfaceMaskPixelsDirty.store(true, std::memory_order_release);
                }
            }

            events::paintBrush::PaintBrushAppliedNotification maskNotification;
            maskNotification.position = worldPosition;
            maskNotification.type = brushType;
            dispatcher.publish(maskNotification);
            return;
        }

        float worldTileSize = 32.0f;
        const auto& allTiles = grid->getAllTiles();
        if (!allTiles.empty())
        {
            worldTileSize = allTiles[0]->config.worldTileSize;
        }

        glm::vec2 brushCenter(worldPosition.x, worldPosition.z);
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            brushCenter, brushParams.radius, worldTileSize);

        auto cacheIt = fileCaches.find(targetEntity->id);
        auto paintFileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
            {
                if (paintFileCache && paintFileCache->hasCoord(coord))
                {
                    streamInTile(*targetEntity, coord.x, coord.z);
                    tile = grid->getTile(coord);
                }
                if (!tile)
                    continue;
            }

            if (paintFileCache && !tile->hasHeightData())
                paintFileCache->ensureHeightsLoaded(*tile);
            if (paintFileCache)
                paintFileCache->markDirty(coord);

            if (!tile->hasWeightMap())
                continue;

            if (brushParams.activeLayer >= terrain::MAX_TERRAIN_LAYERS)
                continue;

            if (brushType == terrain::PaintBrushType::SetBaseLayer)
            {
                uint8_t newBase = static_cast<uint8_t>(brushParams.activeLayer);
                if (tile->weightMap.layerIndices[0] != newBase)
                {
                    tile->weightMap.initializeDefault(tile->weightMap.resolution);
                    tile->weightMap.layerIndices[0] = newBase;
                    tile->weightMapDirty = true;
                    tile->weightMapGPUDirty = true;
                }
                continue;
            }

            terrain::WeightBrushApplicator::ApplyParams applyParams;
            applyParams.brushCenter = brushCenter;
            applyParams.tileWorldOrigin = glm::vec2(
                static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                static_cast<float>(tile->coord.z) * tile->config.worldTileSize);
            applyParams.brushRadius = brushParams.radius;
            applyParams.brushStrength = brushParams.strength;
            applyParams.brushOpacity = brushParams.opacity;
            applyParams.vertexSpacing = tile->config.getVertexSpacing();
            applyParams.verticesPerSide = tile->config.getVertexCount();
            applyParams.falloff = brushParams.falloff;
            applyParams.shape = brushParams.shape;
            applyParams.brushType = brushType;
            applyParams.activeLayer = brushParams.activeLayer;
            applyParams.deltaTime = deltaTime;
            applyParams.invert = invert;

            if (terrain::WeightBrushApplicator::apply(tile->weightMap, applyParams))
            {
                tile->weightMapDirty = true;
                tile->weightMapGPUDirty = true;
            }
        }

        events::paintBrush::PaintBrushAppliedNotification paintNotification;
        paintNotification.position = worldPosition;
        paintNotification.type = brushType;
        dispatcher.publish(paintNotification);

        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(*targetEntity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).saveDirty = true;
            }
        }
    }

    void TerrainService::applyHoleBrush(const glm::vec3& worldPosition, bool erase)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::hole::GetHoleTargetEntityQuery{});
        if (!targetEntity.has_value())
            return;

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
            return;

        terrain::TerrainGrid* grid = gridIt->second.get();

        auto brushParams = dispatcher.query(events::holeBrush::GetHoleBrushParamsQuery{});

        float worldTileSize = 32.0f;
        const auto& allTiles = grid->getAllTiles();
        if (!allTiles.empty())
        {
            worldTileSize = allTiles[0]->config.worldTileSize;
        }

        glm::vec2 brushCenter(worldPosition.x, worldPosition.z);
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            brushCenter, brushParams.radius, worldTileSize);

        auto cacheIt = fileCaches.find(targetEntity->id);
        auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        std::vector<terrain::TileCoord> modifiedTiles;
        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
            {
                if (fileCache && fileCache->hasCoord(coord))
                {
                    streamInTile(*targetEntity, coord.x, coord.z);
                    tile = grid->getTile(coord);
                }
                if (!tile)
                    continue;
            }

            if (fileCache && !tile->hasHeightData())
            {
                if (!fileCache->ensureHeightsLoaded(*tile))
                    continue;
            }

            if (!tile->hasHoleMask())
                tile->initializeHoleMask();

            if (fileCache)
                fileCache->markDirty(coord);

            terrain::HoleBrushApplicator::ApplyParams applyParams;
            applyParams.brushCenter = brushCenter;
            applyParams.tileWorldOrigin = glm::vec2(
                static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                static_cast<float>(tile->coord.z) * tile->config.worldTileSize);
            applyParams.brushRadius = brushParams.radius;
            applyParams.vertexSpacing = tile->config.getVertexSpacing();
            applyParams.quadsPerSide = tile->config.getVertexCount() - 1;
            applyParams.falloff = brushParams.falloff;
            applyParams.shape = brushParams.shape;
            applyParams.erase = erase;

            if (terrain::HoleBrushApplicator::apply(tile->holeMask, applyParams))
            {
                tile->topologyDirty = true;
                tile->isDirty = true;
                tile->setAllLODsDirty();
                modifiedTiles.push_back(coord);
            }
        }

        if (!modifiedTiles.empty())
        {
            syncHoleBoundaries(grid, modifiedTiles);
            rebuildModifiedColliders(*targetEntity, grid, modifiedTiles);

            events::holeBrush::HoleBrushAppliedNotification notification;
            notification.position = worldPosition;
            dispatcher.publish(notification);

            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(*targetEntity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).saveDirty = true;
            }
        }
    }

    void TerrainService::syncHoleBoundaries(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles)
    {
        uint32_t vertexCount = 0;
        const auto& allTiles = grid->getAllTiles();
        if (!allTiles.empty())
            vertexCount = allTiles[0]->config.getVertexCount();
        if (vertexCount < 2)
            return;

        uint32_t quadCount = vertexCount - 1;
        uint32_t lastQuad = quadCount - 1;

        for (const auto& coord : modifiedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile || !tile->hasHoleMask())
                continue;

            // Sync +X neighbor: tile's last quad column == neighbor's first quad column
            terrain::TerrainTile* neighborPX = grid->getTile({coord.x + 1, coord.z});
            if (neighborPX)
            {
                if (!neighborPX->hasHoleMask())
                    neighborPX->initializeHoleMask();

                bool changed = false;
                for (uint32_t z = 0; z < quadCount; ++z)
                {
                    bool holeVal = tile->isHole(lastQuad, z);
                    if (neighborPX->isHole(0, z) != holeVal)
                    {
                        neighborPX->setHole(0, z, holeVal);
                        changed = true;
                    }
                }
                if (changed)
                {
                    neighborPX->topologyDirty = true;
                    neighborPX->isDirty = true;
                    neighborPX->setAllLODsDirty();
                }
            }

            // Sync -X neighbor: tile's first quad column == neighbor's last quad column
            terrain::TerrainTile* neighborNX = grid->getTile({coord.x - 1, coord.z});
            if (neighborNX)
            {
                if (!neighborNX->hasHoleMask())
                    neighborNX->initializeHoleMask();

                bool changed = false;
                for (uint32_t z = 0; z < quadCount; ++z)
                {
                    bool holeVal = tile->isHole(0, z);
                    if (neighborNX->isHole(lastQuad, z) != holeVal)
                    {
                        neighborNX->setHole(lastQuad, z, holeVal);
                        changed = true;
                    }
                }
                if (changed)
                {
                    neighborNX->topologyDirty = true;
                    neighborNX->isDirty = true;
                    neighborNX->setAllLODsDirty();
                }
            }

            // Sync +Z neighbor: tile's last quad row == neighbor's first quad row
            terrain::TerrainTile* neighborPZ = grid->getTile({coord.x, coord.z + 1});
            if (neighborPZ)
            {
                if (!neighborPZ->hasHoleMask())
                    neighborPZ->initializeHoleMask();

                bool changed = false;
                for (uint32_t x = 0; x < quadCount; ++x)
                {
                    bool holeVal = tile->isHole(x, lastQuad);
                    if (neighborPZ->isHole(x, 0) != holeVal)
                    {
                        neighborPZ->setHole(x, 0, holeVal);
                        changed = true;
                    }
                }
                if (changed)
                {
                    neighborPZ->topologyDirty = true;
                    neighborPZ->isDirty = true;
                    neighborPZ->setAllLODsDirty();
                }
            }

            // Sync -Z neighbor: tile's first quad row == neighbor's last quad row
            terrain::TerrainTile* neighborNZ = grid->getTile({coord.x, coord.z - 1});
            if (neighborNZ)
            {
                if (!neighborNZ->hasHoleMask())
                    neighborNZ->initializeHoleMask();

                bool changed = false;
                for (uint32_t x = 0; x < quadCount; ++x)
                {
                    bool holeVal = tile->isHole(x, 0);
                    if (neighborNZ->isHole(x, lastQuad) != holeVal)
                    {
                        neighborNZ->setHole(x, lastQuad, holeVal);
                        changed = true;
                    }
                }
                if (changed)
                {
                    neighborNZ->topologyDirty = true;
                    neighborNZ->isDirty = true;
                    neighborNZ->setAllLODsDirty();
                }
            }
        }
    }

    void TerrainService::applyRamp(EntityHandle targetEntity, terrain::TerrainGrid* grid,
                                    const glm::vec3& startPos, const glm::vec3& endPos,
                                    const terrain::BrushParams& params)
    {
        glm::vec2 start2D(startPos.x, startPos.z);
        glm::vec2 end2D(endPos.x, endPos.z);
        glm::vec2 segDir = end2D - start2D;
        float segLength = glm::length(segDir);
        if (segLength < 0.01f)
            return;

        glm::vec2 segNorm = segDir / segLength;
        float halfWidth = params.rampWidth * 0.5f;
        float totalHalfWidth = halfWidth + params.rampFalloff;

        // Get affected tiles using AABB of segment + width
        auto affectedTiles = terrain::BrushSampler::getAffectedTilesForSegment(
            start2D, end2D, totalHalfWidth,
            grid->getAllTiles().empty() ? 32.0f : grid->getAllTiles()[0]->config.worldTileSize);

        auto cacheIt = fileCaches.find(targetEntity.id);
        auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        std::vector<terrain::TileCoord> modifiedTiles;

        for (const auto& coord : affectedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
                continue;

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

            for (uint32_t z = 0; z < vertCount; ++z)
            {
                for (uint32_t x = 0; x < vertCount; ++x)
                {
                    glm::vec2 vertPos = tileOrigin + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * vertSpacing;

                    // Project vertex onto line segment
                    glm::vec2 toVert = vertPos - start2D;
                    float t = glm::dot(toVert, segNorm) / segLength;
                    t = glm::clamp(t, 0.0f, 1.0f);

                    // Closest point on segment
                    glm::vec2 closestPoint = start2D + segDir * t;
                    float perpDist = glm::length(vertPos - closestPoint);

                    if (perpDist > totalHalfWidth)
                        continue;

                    // Target height: linear interpolation along ramp
                    float targetHeight = glm::mix(startPos.y, endPos.y, t);

                    // Blend factor based on perpendicular distance
                    float blend = 1.0f;
                    if (perpDist > halfWidth && params.rampFalloff > 0.0f)
                    {
                        float falloffT = (perpDist - halfWidth) / params.rampFalloff;
                        blend = 1.0f - falloffT * falloffT * (3.0f - 2.0f * falloffT); // smoothstep
                    }

                    uint32_t idx = z * vertCount + x;
                    float currentHeight = tile->heightData[idx];
                    tile->heightData[idx] = glm::mix(currentHeight, targetHeight, blend);
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

        syncBrushBoundaryHeights(grid, modifiedTiles);

        events::brush::BrushAppliedNotification notification;
        notification.position = endPos;
        notification.type = terrain::BrushType::Ramp;
        events::EventDispatcher::instance().publish(notification);

        rebuildModifiedColliders(targetEntity, grid, modifiedTiles);
    }

    void TerrainService::syncBrushBoundaryHeights(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles)
    {
        if (modifiedTiles.empty())
            return;

        for (const auto& coord : modifiedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile || !tile->hasHeightData())
                continue;

            uint32_t vertCount = tile->config.getVertexCount();
            uint32_t lastIdx = vertCount - 1;

            // Sync +X boundary: tile's last column == neighbor's first column
            terrain::TerrainTile* neighborPX = grid->getTile({coord.x + 1, coord.z});
            if (neighborPX && neighborPX->hasHeightData())
            {
                for (uint32_t z = 0; z < vertCount; ++z)
                {
                    float avg = (tile->heightData[z * vertCount + lastIdx] +
                                 neighborPX->heightData[z * vertCount]) * 0.5f;
                    tile->heightData[z * vertCount + lastIdx] = avg;
                    neighborPX->heightData[z * vertCount] = avg;
                }
                neighborPX->isDirty = true;
                neighborPX->setAllLODsDirty();
            }

            // Sync +Z boundary: tile's last row == neighbor's first row
            terrain::TerrainTile* neighborPZ = grid->getTile({coord.x, coord.z + 1});
            if (neighborPZ && neighborPZ->hasHeightData())
            {
                for (uint32_t x = 0; x < vertCount; ++x)
                {
                    float avg = (tile->heightData[lastIdx * vertCount + x] +
                                 neighborPZ->heightData[x]) * 0.5f;
                    tile->heightData[lastIdx * vertCount + x] = avg;
                    neighborPZ->heightData[x] = avg;
                }
                neighborPZ->isDirty = true;
                neighborPZ->setAllLODsDirty();
            }
        }
    }

    // Vegetation brush is now handled by VegetationBrushServiceImpl (instance-based placement)

}
