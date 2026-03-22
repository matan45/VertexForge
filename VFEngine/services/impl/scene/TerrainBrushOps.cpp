#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/BrushSampler.hpp"
#include "terrain/WeightBrushApplicator.hpp"
#include "terrain/HoleBrushApplicator.hpp"
#include "terrain/CaveBrushApplicator.hpp"
#include "terrain/CaveMeshGenerator.hpp"
#include "vegetation/VegetationDensityBrushApplicator.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/PaintBrushEvents.hpp"
#include "../../events/terrain/HoleBrushEvents.hpp"
#include "../../events/terrain/CaveBrushEvents.hpp"
#include "../../events/terrain/CaveModeEvents.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "../../events/terrain/PaintModeEvents.hpp"
#include "../../events/terrain/HoleModeEvents.hpp"

namespace services
{
    void TerrainService::applyBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire) || svtBakeInProgress.load(std::memory_order_acquire))
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
            gpuParams.invert = invert;

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

        events::brush::BrushAppliedNotification notification;
        notification.position = worldPosition;
        notification.type = brushType;
        dispatcher.publish(notification);

        rebuildModifiedColliders(*targetEntity, grid, modifiedTiles);
    }

    void TerrainService::applyPaintBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire) || svtBakeInProgress.load(std::memory_order_acquire))
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
        if (saveInProgress.load(std::memory_order_acquire) || svtBakeInProgress.load(std::memory_order_acquire))
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

    void TerrainService::applyVegetationDensityBrush(
        const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire) || svtBakeInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::vegetationBrush::GetVegetationBrushTargetEntityQuery{});
        if (!targetEntity.has_value())
            return;

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
            return;

        terrain::TerrainGrid* grid = gridIt->second.get();

        auto brushParams = dispatcher.query(events::vegetationBrush::GetDensityBrushParamsQuery{});
        auto brushType = dispatcher.query(events::vegetationBrush::GetDensityBrushTypeQuery{});

        float worldTileSize = 32.0f;
        const auto& allTiles = grid->getAllTiles();
        if (!allTiles.empty())
            worldTileSize = allTiles[0]->config.worldTileSize;

        glm::vec2 brushCenter(worldPosition.x, worldPosition.z);
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            brushCenter, brushParams.radius, worldTileSize);

        auto cacheIt = fileCaches.find(targetEntity->id);
        auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        bool anyModified = false;
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

            if (!tile->vegetationDensity.isInitialized())
                tile->vegetationDensity.initializeDefault(tile->config.getVertexCount());

            if (fileCache)
                fileCache->markDirty(coord);

            vegetation::VegetationDensityBrushApplicator::ApplyParams applyParams;
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
            applyParams.deltaTime = deltaTime;
            applyParams.invert = invert;

            if (vegetation::VegetationDensityBrushApplicator::apply(tile->vegetationDensity, applyParams))
            {
                tile->vegetationDensityDirty = true;
                tile->vegetationDensityGPUDirty = true;
                anyModified = true;
            }
        }

        if (anyModified)
        {
            events::vegetationBrush::VegetationDensityBrushAppliedNotification notification;
            notification.position = worldPosition;
            notification.type = brushType;
            dispatcher.publish(notification);

            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(*targetEntity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).saveDirty = true;
            }
        }
    }

    void TerrainService::applyCaveBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire) || svtBakeInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::cave::GetCaveTargetEntityQuery{});
        if (!targetEntity.has_value())
            return;

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
            return;

        terrain::TerrainGrid* grid = gridIt->second.get();

        auto brushType = dispatcher.query(events::caveBrush::GetCaveBrushTypeQuery{});
        auto brushParams = dispatcher.query(events::caveBrush::GetCaveBrushParamsQuery{});

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
            if (fileCache)
                fileCache->markDirty(coord);

            // Initialize SDF from heightmap (captures the terrain surface)
            if (!tile->hasCaveData())
                tile->initializeCaveSDFFromHeights();

            if (!tile->hasCaveData())
                continue;

            // Apply 3D SDF carve — works in all directions (down, horizontal, up)
            terrain::CaveBrushApplicator::ApplyParams applyParams;
            applyParams.brushCenter = worldPosition;
            applyParams.tileWorldOrigin = glm::vec2(
                static_cast<float>(tile->coord.x) * tile->config.worldTileSize,
                static_cast<float>(tile->coord.z) * tile->config.worldTileSize);
            applyParams.brushRadius = brushParams.radius;
            applyParams.brushStrength = brushParams.strength;
            applyParams.brushType = brushType;
            applyParams.falloff = brushParams.falloff;
            applyParams.shape = brushParams.shape;
            applyParams.deltaTime = deltaTime;
            applyParams.invert = invert;

            if (!terrain::CaveBrushApplicator::apply(*tile->caveData, applyParams))
                continue;

            // Regenerate cave mesh (Marching Cubes on modified region only)
            terrain::CaveMeshGenerator::generate(*tile);

            tile->caveDirty = true;
            tile->caveGPUDirty = true;
            tile->isDirty = true;
            modifiedTiles.push_back(coord);
        }

        if (!modifiedTiles.empty())
        {
            events::caveBrush::CaveBrushAppliedNotification notification;
            notification.position = worldPosition;
            notification.type = brushType;
            dispatcher.publish(notification);
        }
    }

    void TerrainService::finalizeCaveBrush()
    {
        if (saveInProgress.load(std::memory_order_acquire) || svtBakeInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();
        auto targetEntity = dispatcher.query(events::cave::GetCaveTargetEntityQuery{});
        if (!targetEntity.has_value())
            return;

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
            return;

        terrain::TerrainGrid* grid = gridIt->second.get();
        const auto& allTiles = grid->getAllTiles();

        std::vector<terrain::TileCoord> caveTiles;

        for (auto* tile : allTiles)
        {
            if (!tile || !tile->hasCaveData() || !tile->caveData->hasCaveGeometry())
                continue;

            // Punch holes in heightmap where cave reaches the surface
            if (!tile->hasHoleMask())
                tile->initializeHoleMask();

            uint32_t vertexCount = tile->config.getVertexCount();
            uint32_t quadCount = vertexCount - 1;
            const auto& sdf = *tile->caveData;
            bool holesChanged = false;

            for (uint32_t qz = 0; qz < quadCount; ++qz)
            {
                for (uint32_t qx = 0; qx < quadCount; ++qx)
                {
                    bool shouldBeHole = false;
                    for (int dz = 0; dz <= 1 && !shouldBeHole; ++dz)
                    {
                        for (int dx = 0; dx <= 1 && !shouldBeHole; ++dx)
                        {
                            uint32_t vx = qx + dx;
                            uint32_t vz = qz + dz;
                            float surfaceHeight = tile->heightData[vz * vertexCount + vx];

                            glm::vec3 surfacePos(
                                tile->worldOrigin.x + vx * tile->config.getVertexSpacing(),
                                surfaceHeight,
                                tile->worldOrigin.z + vz * tile->config.getVertexSpacing());

                            float currentSdf = sdf.sampleSDF(surfacePos);
                            if (currentSdf > 0.1f)
                                shouldBeHole = true;
                        }
                    }

                    bool isCurrentlyHole = tile->isHole(qx, qz);
                    if (shouldBeHole != isCurrentlyHole)
                    {
                        tile->setHole(qx, qz, shouldBeHole);
                        holesChanged = true;
                    }
                }
            }

            if (holesChanged)
            {
                tile->topologyDirty = true;
                tile->setAllLODsDirty();
                tile->isDirty = true;
            }

            caveTiles.push_back(tile->coord);
        }

        if (!caveTiles.empty())
        {
            syncCaveBoundaries(grid, caveTiles);
            rebuildModifiedColliders(*targetEntity, grid, caveTiles);

            // Rebuild cave physics colliders
            if (physicsProvider && physicsProvider->hasTerrainCollider(*targetEntity))
            {
                for (const auto& coord : caveTiles)
                {
                    auto* tile = grid->getTile(coord);
                    if (tile && tile->hasCaveGeometry() && !tile->caveLOD.isEmpty())
                    {
                        glm::vec3 tileOriginOffset(tile->worldOrigin.x, 0.0f, tile->worldOrigin.z);
                        std::vector<glm::vec3> worldPositions;
                        worldPositions.reserve(tile->caveLOD.vertices.size());
                        for (const auto& v : tile->caveLOD.vertices)
                            worldPositions.push_back(v.position + tileOriginOffset);

                        CaveTileColliderInfo caveInfo;
                        caveInfo.tileX = coord.x;
                        caveInfo.tileZ = coord.z;
                        caveInfo.vertices = worldPositions.data();
                        caveInfo.vertexCount = static_cast<uint32_t>(worldPositions.size());
                        caveInfo.indices = tile->caveLOD.indices.data();
                        caveInfo.indexCount = static_cast<uint32_t>(tile->caveLOD.indices.size());

                        physicsProvider->rebuildCaveTileCollider(*targetEntity, caveInfo);
                    }
                }
            }

            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(*targetEntity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).saveDirty = true;
            }
        }
    }

    void TerrainService::syncCaveBoundaries(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles)
    {
        // Sync SDF ghost voxels at tile boundaries for seamless Marching Cubes
        for (const auto& coord : modifiedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile || !tile->hasCaveData())
                continue;

            auto& sdf = *tile->caveData;

            // Sync +X neighbor: copy last column of this tile to first column of neighbor
            terrain::TerrainTile* neighborPX = grid->getTile({coord.x + 1, coord.z});
            if (neighborPX && neighborPX->hasCaveData())
            {
                auto& nSdf = *neighborPX->caveData;
                uint32_t lastX = sdf.config.resX - 1;
                bool changed = false;
                for (uint32_t y = 0; y < sdf.config.resY; ++y)
                {
                    for (uint32_t z = 0; z < sdf.config.resZ; ++z)
                    {
                        float myVal = sdf.getSDF(lastX, y, z);
                        float nVal = nSdf.getSDF(0, y, z);
                        float avg = (myVal + nVal) * 0.5f;
                        sdf.setSDF(lastX, y, z, avg);
                        nSdf.setSDF(0, y, z, avg);
                        changed = true;
                    }
                }
                if (changed)
                {
                    nSdf.isDirty = true;
                    terrain::CaveMeshGenerator::generate(*neighborPX);
                    neighborPX->caveDirty = true;
                    neighborPX->caveGPUDirty = true;
                }
            }

            // Sync +Z neighbor: copy last row of this tile to first row of neighbor
            terrain::TerrainTile* neighborPZ = grid->getTile({coord.x, coord.z + 1});
            if (neighborPZ && neighborPZ->hasCaveData())
            {
                auto& nSdf = *neighborPZ->caveData;
                uint32_t lastZ = sdf.config.resZ - 1;
                bool changed = false;
                for (uint32_t y = 0; y < sdf.config.resY; ++y)
                {
                    for (uint32_t x = 0; x < sdf.config.resX; ++x)
                    {
                        float myVal = sdf.getSDF(x, y, lastZ);
                        float nVal = nSdf.getSDF(x, y, 0);
                        float avg = (myVal + nVal) * 0.5f;
                        sdf.setSDF(x, y, lastZ, avg);
                        nSdf.setSDF(x, y, 0, avg);
                        changed = true;
                    }
                }
                if (changed)
                {
                    nSdf.isDirty = true;
                    terrain::CaveMeshGenerator::generate(*neighborPZ);
                    neighborPZ->caveDirty = true;
                    neighborPZ->caveGPUDirty = true;
                }
            }
        }
    }

}
