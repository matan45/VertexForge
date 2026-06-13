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
#include "../../events/editor/UndoRedoEvents.hpp"
#include "../../data/CaveUndoCommands.hpp"
#include <cmath>
#include <memory>
#include <unordered_set>

namespace services
{
    static constexpr float CAVE_HOLE_PUNCH_THRESHOLD = 0.1f;

    // Gather the +X / +Z / +X+Z neighbour SDF grids so the mesher can extend a one-cell
    // apron across the shared boundary and produce crack-free seams.
    static terrain::NeighborCaves buildNeighborCaves(terrain::TerrainGrid* grid, const terrain::TileCoord& coord)
    {
        terrain::NeighborCaves nc;
        if (auto* px = grid->getTile({coord.x + 1, coord.z}); px && px->hasCaveData())
            nc.plusX = px->caveData.get();
        if (auto* pz = grid->getTile({coord.x, coord.z + 1}); pz && pz->hasCaveData())
            nc.plusZ = pz->caveData.get();
        if (auto* pxz = grid->getTile({coord.x + 1, coord.z + 1}); pxz && pxz->hasCaveData())
            nc.plusXZ = pxz->caveData.get();
        return nc;
    }

    void TerrainService::applyCaveBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication)
    {
        if (saveInProgress.load(std::memory_order_acquire))
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::cave::GetCaveTargetEntityQuery{});
        if (!targetEntity.has_value())
            return;

        auto gridIt = terrainGrids.find(targetEntity->id);
        if (gridIt == terrainGrids.end())
            return;

        terrain::TerrainGrid* grid = gridIt->second.get();

        // Start of a new stroke: drop any stale before-snapshots and remember the target.
        if (isFirstApplication)
        {
            caveStrokeBefore.clear();
            caveStrokeEntityId = targetEntity->id;
        }

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

            // Capture the tile's pre-stroke state once, before its first modification,
            // so the whole stroke can be undone/redone.
            if (caveStrokeBefore.find(coord) == caveStrokeBefore.end())
                caveStrokeBefore[coord] = {tile->caveData->sdfGrid, tile->holeMask};

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

            // Regenerate cave mesh (Surface Nets on modified region, apron-stitched to neighbours)
            terrain::CaveMeshGenerator::generate(*tile, buildNeighborCaves(grid, coord));

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
        if (saveInProgress.load(std::memory_order_acquire))
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

        // Build one undo entry for the whole stroke from the captured before-states.
        if (!caveStrokeBefore.empty())
        {
            auto undoCmd = std::make_shared<CaveStrokeUndoCommand>(caveStrokeEntityId, "Cave Brush");
            for (auto& [coord, before] : caveStrokeBefore)
            {
                terrain::TerrainTile* tile = grid->getTile(coord);
                std::vector<float> afterSdf = (tile && tile->hasCaveData())
                    ? tile->caveData->sdfGrid : std::vector<float>{};
                std::vector<uint8_t> afterHole = tile ? tile->holeMask : std::vector<uint8_t>{};
                undoCmd->addTile(coord.x, coord.z,
                                 std::move(before.sdf), std::move(afterSdf),
                                 std::move(before.holeMask), std::move(afterHole));
            }
            if (undoCmd->hasChanges())
            {
                events::undoredo::PushUndoableCommand pushCmd;
                pushCmd.command = undoCmd;
                dispatcher.execute(pushCmd);
            }
            caveStrokeBefore.clear();
        }
    }

    void TerrainService::restoreCaveState(uint64_t entityId,
                                          const std::vector<::events::caveBrush::CaveTileState>& tiles)
    {
        auto gridIt = terrainGrids.find(entityId);
        if (gridIt == terrainGrids.end())
            return;
        terrain::TerrainGrid* grid = gridIt->second.get();

        std::vector<terrain::TileCoord> coords;
        coords.reserve(tiles.size());
        for (const auto& st : tiles)
        {
            terrain::TileCoord coord{st.tileX, st.tileZ};
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile)
                continue;

            if (!st.sdf.empty())
            {
                if (!tile->hasCaveData())
                    tile->initializeCaveSDFFromHeights();
                if (tile->hasCaveData() && tile->caveData->sdfGrid.size() == st.sdf.size())
                {
                    tile->caveData->sdfGrid = st.sdf;
                    tile->caveData->isDirty = true;
                    tile->caveData->clearDirtyRegion(); // full re-mesh, not incremental
                }
            }

            // Restored to pristine (no carve left): drop the stale cave mesh on the CPU.
            if (tile->hasCaveData() && !tile->caveData->hasCaveGeometry())
                tile->caveLOD.clear();

            tile->holeMask = st.holeMask;
            tile->caveDirty = true;
            tile->caveGPUDirty = true;
            tile->topologyDirty = true;
            tile->setAllLODsDirty();
            tile->isDirty = true;
            coords.push_back(coord);
        }

        if (coords.empty())
            return;

        // Remesh restored tiles + their seam neighbours with apron stitching.
        std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> toRemesh;
        for (const auto& coord : coords)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    toRemesh.insert({coord.x + dx, coord.z + dz});
        for (const auto& coord : toRemesh)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile || !tile->hasCaveData() || !tile->caveData->hasCaveGeometry())
                continue;
            terrain::CaveMeshGenerator::generate(*tile, buildNeighborCaves(grid, coord));
            tile->caveDirty = true;
            tile->caveGPUDirty = true;
        }

        EntityHandle entity{entityId};
        rebuildModifiedColliders(entity, grid, coords);
        rebuildCaveColliders(entity, grid, coords);

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);
        if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
        {
            registry.get<components::TerrainComponent>(ent).saveDirty = true;
        }
    }

    void TerrainService::syncCaveNeighborEdge(
        terrain::CaveSDFData& sdf,
        terrain::TerrainTile& neighbor,
        int axis)
    {
        // Deterministic weld (replaces the old order-dependent averaging): the lower-
        // coordinate tile (`sdf`) is authoritative, so copy its shared boundary column
        // into the higher-coordinate neighbour's duplicated column. The apron mesher then
        // reads identical data from both sides, so the seam meshes coincide.
        auto& nSdf = *neighbor.caveData;
        bool changed = false;

        if (axis == 0) // +X neighbour: my last X column -> neighbour's column 0
        {
            uint32_t lastX = sdf.config.resX - 1;
            for (uint32_t y = 0; y < sdf.config.resY; ++y)
                for (uint32_t z = 0; z < sdf.config.resZ; ++z)
                {
                    float v = sdf.getSDF(lastX, y, z);
                    if (nSdf.getSDF(0, y, z) != v)
                    {
                        nSdf.setSDF(0, y, z, v);
                        changed = true;
                    }
                }
        }
        else // +Z neighbour: my last Z row -> neighbour's row 0
        {
            uint32_t lastZ = sdf.config.resZ - 1;
            for (uint32_t y = 0; y < sdf.config.resY; ++y)
                for (uint32_t x = 0; x < sdf.config.resX; ++x)
                {
                    float v = sdf.getSDF(x, y, lastZ);
                    if (nSdf.getSDF(x, y, 0) != v)
                    {
                        nSdf.setSDF(x, y, 0, v);
                        changed = true;
                    }
                }
        }

        if (changed)
            nSdf.isDirty = true;
    }

    void TerrainService::syncCaveBoundaries(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles)
    {
        // 1. Weld shared boundary columns (lower-coord tile authoritative).
        for (const auto& coord : modifiedTiles)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile || !tile->hasCaveData())
                continue;

            if (auto* px = grid->getTile({coord.x + 1, coord.z}); px && px->hasCaveData())
                syncCaveNeighborEdge(*tile->caveData, *px, 0);
            if (auto* pz = grid->getTile({coord.x, coord.z + 1}); pz && pz->hasCaveData())
                syncCaveNeighborEdge(*tile->caveData, *pz, 1);
        }

        // 2. Remesh every tile whose mesh could be affected — the modified tiles and all
        //    neighbours touching their seams — each with its own apron so adjacent meshes
        //    coincide along the (now welded) boundaries.
        std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> toRemesh;
        for (const auto& coord : modifiedTiles)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    toRemesh.insert({coord.x + dx, coord.z + dz});

        for (const auto& coord : toRemesh)
        {
            terrain::TerrainTile* tile = grid->getTile(coord);
            if (!tile || !tile->hasCaveData() || !tile->caveData->hasCaveGeometry())
                continue;

            terrain::CaveMeshGenerator::generate(*tile, buildNeighborCaves(grid, coord));
            tile->caveDirty = true;
            tile->caveGPUDirty = true;
        }
    }

}
