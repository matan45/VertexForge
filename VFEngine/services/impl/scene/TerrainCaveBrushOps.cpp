#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/BrushSampler.hpp"
#include "terrain/CaveBrushApplicator.hpp"
#include "terrain/CaveMeshGenerator.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/CaveBrushEvents.hpp"
#include "../../events/terrain/CaveModeEvents.hpp"
#include <cmath>

namespace services
{
    static constexpr float CAVE_HOLE_PUNCH_THRESHOLD = 0.1f;

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

    void TerrainService::punchCaveHolesForTile(terrain::TerrainTile& tile)
    {
        if (!tile.hasHoleMask())
            tile.initializeHoleMask();

        uint32_t vertexCount = tile.config.getVertexCount();
        uint32_t quadCount = vertexCount - 1;
        const auto& sdf = *tile.caveData;
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
                        float surfaceHeight = tile.heightData[vz * vertexCount + vx];

                        glm::vec3 surfacePos(
                            tile.worldOrigin.x + vx * tile.config.getVertexSpacing(),
                            surfaceHeight,
                            tile.worldOrigin.z + vz * tile.config.getVertexSpacing());

                        float currentSdf = sdf.sampleSDF(surfacePos);
                        if (currentSdf > CAVE_HOLE_PUNCH_THRESHOLD)
                            shouldBeHole = true;
                    }
                }

                bool isCurrentlyHole = tile.isHole(qx, qz);
                if (shouldBeHole != isCurrentlyHole)
                {
                    tile.setHole(qx, qz, shouldBeHole);
                    holesChanged = true;
                }
            }
        }

        if (holesChanged)
        {
            tile.topologyDirty = true;
            tile.setAllLODsDirty();
            tile.isDirty = true;
        }
    }

    CaveTileColliderInfo TerrainService::buildCaveTileColliderInfo(
        const terrain::TerrainTile& tile,
        std::vector<glm::vec3>& worldPositionsOut)
    {
        glm::vec3 tileOriginOffset(tile.worldOrigin.x, 0.0f, tile.worldOrigin.z);
        worldPositionsOut.clear();
        worldPositionsOut.reserve(tile.caveLOD.vertices.size());
        for (const auto& v : tile.caveLOD.vertices)
            worldPositionsOut.push_back(v.position + tileOriginOffset);

        CaveTileColliderInfo caveInfo;
        caveInfo.tileX = tile.coord.x;
        caveInfo.tileZ = tile.coord.z;
        caveInfo.vertices = worldPositionsOut.data();
        caveInfo.vertexCount = static_cast<uint32_t>(worldPositionsOut.size());
        caveInfo.indices = tile.caveLOD.indices.data();
        caveInfo.indexCount = static_cast<uint32_t>(tile.caveLOD.indices.size());
        return caveInfo;
    }

    void TerrainService::rebuildCaveColliders(
        EntityHandle entity,
        terrain::TerrainGrid* grid,
        const std::vector<terrain::TileCoord>& caveTiles)
    {
        if (!physicsProvider || !physicsProvider->hasTerrainCollider(entity))
            return;

        for (const auto& coord : caveTiles)
        {
            auto* tile = grid->getTile(coord);
            if (tile && tile->hasCaveGeometry() && !tile->caveLOD.isEmpty())
            {
                std::vector<glm::vec3> worldPositions;
                auto caveInfo = buildCaveTileColliderInfo(*tile, worldPositions);
                physicsProvider->rebuildCaveTileCollider(entity, caveInfo);
            }
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
            if (!tile || !tile->hasCaveData() || !tile->caveData->hasDirtyRegion)
                continue;

            if (!tile->caveData->hasCaveGeometry())
                continue;

            punchCaveHolesForTile(*tile);
            caveTiles.push_back(tile->coord);
        }

        if (!caveTiles.empty())
        {
            syncCaveBoundaries(grid, caveTiles);
            rebuildModifiedColliders(*targetEntity, grid, caveTiles);
            rebuildCaveColliders(*targetEntity, grid, caveTiles);

            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(*targetEntity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).saveDirty = true;
            }
        }
    }

    void TerrainService::syncCaveNeighborEdge(
        terrain::CaveSDFData& sdf,
        terrain::TerrainTile& neighbor,
        int axis)
    {
        auto& nSdf = *neighbor.caveData;
        bool changed = false;

        if (axis == 0) // X axis
        {
            uint32_t lastX = sdf.config.resX - 1;
            for (uint32_t y = 0; y < sdf.config.resY; ++y)
            {
                for (uint32_t z = 0; z < sdf.config.resZ; ++z)
                {
                    float myVal = sdf.getSDF(lastX, y, z);
                    float nVal = nSdf.getSDF(0, y, z);
                    float avg = (myVal + nVal) * 0.5f;
                    if (std::abs(avg - myVal) > 1e-6f || std::abs(avg - nVal) > 1e-6f)
                    {
                        sdf.setSDF(lastX, y, z, avg);
                        nSdf.setSDF(0, y, z, avg);
                        changed = true;
                    }
                }
            }
        }
        else // Z axis
        {
            uint32_t lastZ = sdf.config.resZ - 1;
            for (uint32_t y = 0; y < sdf.config.resY; ++y)
            {
                for (uint32_t x = 0; x < sdf.config.resX; ++x)
                {
                    float myVal = sdf.getSDF(x, y, lastZ);
                    float nVal = nSdf.getSDF(x, y, 0);
                    float avg = (myVal + nVal) * 0.5f;
                    if (std::abs(avg - myVal) > 1e-6f || std::abs(avg - nVal) > 1e-6f)
                    {
                        sdf.setSDF(x, y, lastZ, avg);
                        nSdf.setSDF(x, y, 0, avg);
                        changed = true;
                    }
                }
            }
        }

        if (changed)
        {
            nSdf.isDirty = true;
            terrain::CaveMeshGenerator::generate(neighbor);
            neighbor.caveDirty = true;
            neighbor.caveGPUDirty = true;
        }
    }

    void TerrainService::syncCaveBoundaries(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles)
    {
        for (const auto& coord : modifiedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile || !tile->hasCaveData())
                continue;

            auto& sdf = *tile->caveData;

            // Sync +X neighbor
            terrain::TerrainTile* neighborPX = grid->getTile({coord.x + 1, coord.z});
            if (neighborPX && neighborPX->hasCaveData())
                syncCaveNeighborEdge(sdf, *neighborPX, 0);

            // Sync +Z neighbor
            terrain::TerrainTile* neighborPZ = grid->getTile({coord.x, coord.z + 1});
            if (neighborPZ && neighborPZ->hasCaveData())
                syncCaveNeighborEdge(sdf, *neighborPZ, 1);
        }
    }

}
