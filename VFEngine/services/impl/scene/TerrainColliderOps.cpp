#include "TerrainService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "../../data/EntityConversion.hpp"
#include <algorithm>
#include <cfloat>

namespace services
{
    bool TerrainService::isVertexAdjacentToHole(const terrain::TerrainTile& tile, uint32_t vx, uint32_t vz)
    {
        if (!tile.hasHoleMask()) return false;
        uint32_t qc = tile.config.getVertexCount() - 1;
        for (int dz = -1; dz <= 0; ++dz)
        {
            for (int dx = -1; dx <= 0; ++dx)
            {
                int qx = static_cast<int>(vx) + dx;
                int qz = static_cast<int>(vz) + dz;
                if (qx >= 0 && qx < static_cast<int>(qc) &&
                    qz >= 0 && qz < static_cast<int>(qc))
                {
                    if (tile.holeMask[static_cast<size_t>(qz) * qc + qx])
                        return true;
                }
            }
        }
        return false;
    }

    void TerrainService::generateTileColliderWireframe(
        const terrain::TerrainTile& tile,
        components::TerrainColliderDebugData& out)
    {
        if (!tile.hasHeightData())
            return;

        uint32_t vertexCount = tile.config.getVertexCount();
        float spacing = tile.config.getVertexSpacing();
        float originX = tile.worldOrigin.x;
        float originZ = tile.worldOrigin.z;

        out.vertices.resize(vertexCount * vertexCount);
        for (uint32_t z = 0; z < vertexCount; ++z)
        {
            for (uint32_t x = 0; x < vertexCount; ++x)
            {
                float height = tile.heightData[z * vertexCount + x];
                out.vertices[z * vertexCount + x] = glm::vec3(
                    originX + x * spacing,
                    height,
                    originZ + z * spacing
                );
            }
        }

        out.lineIndices.clear();

        for (uint32_t z = 0; z < vertexCount; ++z)
        {
            for (uint32_t x = 0; x < vertexCount - 1; ++x)
            {
                if (isVertexAdjacentToHole(tile, x, z) && isVertexAdjacentToHole(tile, x + 1, z))
                    continue;
                out.lineIndices.push_back(z * vertexCount + x);
                out.lineIndices.push_back(z * vertexCount + x + 1);
            }
        }

        for (uint32_t x = 0; x < vertexCount; ++x)
        {
            for (uint32_t z = 0; z < vertexCount - 1; ++z)
            {
                if (isVertexAdjacentToHole(tile, x, z) && isVertexAdjacentToHole(tile, x, z + 1))
                    continue;
                out.lineIndices.push_back(z * vertexCount + x);
                out.lineIndices.push_back((z + 1) * vertexCount + x);
            }
        }

        appendCaveWireframe(tile, out);

        out.version++;
    }

    bool TerrainService::applyHoleMaskToHeights(const terrain::TerrainTile& tile, std::vector<float>& physicsHeights)
    {
        if (!tile.hasHoleMask()) return false;

        bool hasAnyHole = false;
        for (uint8_t h : tile.holeMask)
        {
            if (h) { hasAnyHole = true; break; }
        }
        if (!hasAnyHole) return false;

        uint32_t vc = tile.config.getVertexCount();
        physicsHeights = tile.heightData;

        for (uint32_t vz = 0; vz < vc; ++vz)
        {
            for (uint32_t vx = 0; vx < vc; ++vx)
            {
                if (isVertexAdjacentToHole(tile, vx, vz))
                    physicsHeights[static_cast<size_t>(vz) * vc + vx] = FLT_MAX;
            }
        }
        return true;
    }

    TerrainTileColliderInfo TerrainService::buildTileColliderInfo(const terrain::TerrainTile& tile,
                                                                     EntityHandle terrainEntity,
                                                                     std::vector<float>& physicsHeightsOut) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(terrainEntity);
        float friction = 0.5f;
        float restitution = 0.0f;
        uint8_t collisionLayer = 0;
        if (registry.valid(ent) && registry.all_of<components::TerrainColliderComponent>(ent))
        {
            const auto& cc = registry.get<components::TerrainColliderComponent>(ent);
            friction = cc.friction;
            restitution = cc.restitution;
            collisionLayer = cc.collisionLayer;
        }

        TerrainTileColliderInfo info;
        info.tileX = tile.coord.x;
        info.tileZ = tile.coord.z;
        info.heightSamples = tile.heightData.data();
        info.sampleCount = tile.config.getVertexCount();
        info.worldOrigin = tile.worldOrigin;
        info.vertexSpacing = tile.config.getVertexSpacing();
        info.friction = friction;
        info.restitution = restitution;
        info.collisionLayer = collisionLayer;

        if (applyHoleMaskToHeights(tile, physicsHeightsOut))
            info.heightSamples = physicsHeightsOut.data();

        return info;
    }

    bool TerrainService::addTerrainCollider(EntityHandle terrainEntity)
    {
        if (!physicsProvider || !terrainEntity.isValid())
            return false;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return false;

        auto* grid = gridIt->second.get();
        const auto& allTiles = grid->getAllTiles();

        auto cacheIt = fileCaches.find(terrainEntity.id);
        auto fileCache = (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        uint32_t submittedCount = 0;

        for (auto* tile : allTiles)
        {
            if (!tile)
                continue;

            if (fileCache && !tile->hasHeightData())
            {
                if (!fileCache->ensureHeightsLoaded(*tile))
                    continue;
            }

            if (!tile->hasHeightData())
                continue;

            std::vector<float> physicsHeights;
            auto info = buildTileColliderInfo(*tile, terrainEntity, physicsHeights);

            // Initial creation uses distance 0 (full LOD) since no camera position available
            physicsProvider->submitAsyncTerrainTileCollider(terrainEntity, info, 0.0f);
            ++submittedCount;
        }

        if (submittedCount == 0)
            return false;

        appendCaveColliders(terrainEntity, allTiles);

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(terrainEntity);
        if (registry.valid(ent))
        {
            if (!registry.all_of<components::TerrainColliderComponent>(ent))
            {
                registry.emplace<components::TerrainColliderComponent>(ent);
            }
            registry.get<components::TerrainColliderComponent>(ent).hasCollider = true;
        }

        generateDebugWireframes(terrainEntity, grid);

        vfLogInfo("TerrainService: Submitted {} terrain tile colliders async", submittedCount);
        return true;
    }

    void TerrainService::removeTerrainCollider(EntityHandle terrainEntity)
    {
        if (!physicsProvider || !terrainEntity.isValid())
            return;

        physicsProvider->removeTerrainCollider(terrainEntity);

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(terrainEntity);
        if (registry.valid(ent))
        {
            if (registry.all_of<components::TerrainColliderComponent>(ent))
                registry.remove<components::TerrainColliderComponent>(ent);

            if (registry.all_of<components::ChildrenComponent>(ent))
            {
                const auto& children = registry.get<components::ChildrenComponent>(ent).children;
                for (auto childEnt : children)
                {
                    if (registry.valid(childEnt) &&
                        registry.all_of<components::TerrainTileColliderDebugComponent>(childEnt))
                    {
                        registry.remove<components::TerrainTileColliderDebugComponent>(childEnt);
                    }
                }
            }
        }

        pendingPhysicsTiles.erase(
            std::remove_if(pendingPhysicsTiles.begin(), pendingPhysicsTiles.end(),
                [&](const auto& p) { return p.first == terrainEntity.id; }),
            pendingPhysicsTiles.end());

        vfLogInfo("TerrainService: Removed terrain collider");
    }

    bool TerrainService::hasTerrainCollider(EntityHandle terrainEntity) const
    {
        if (!physicsProvider || !terrainEntity.isValid())
            return false;

        return physicsProvider->hasTerrainCollider(terrainEntity);
    }

    void TerrainService::generateDebugWireframes(EntityHandle terrainEntity, terrain::TerrainGrid* grid)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(terrainEntity);

        if (!registry.valid(ent) || !registry.all_of<components::ChildrenComponent>(ent))
            return;

        const auto& children = registry.get<components::ChildrenComponent>(ent).children;
        for (auto childEnt : children)
        {
            if (!registry.valid(childEnt) ||
                !registry.all_of<components::TerrainTileComponent>(childEnt))
                continue;

            const auto& tileComp = registry.get<components::TerrainTileComponent>(childEnt);
            terrain::TileCoord coord{tileComp.tileX, tileComp.tileZ};
            auto* tile = grid->getTile(coord);
            if (!tile || !tile->hasHeightData())
                continue;

            auto& debugComp = registry.emplace_or_replace<components::TerrainTileColliderDebugComponent>(childEnt);
            debugComp.tileX = tileComp.tileX;
            debugComp.tileZ = tileComp.tileZ;
            generateTileColliderWireframe(*tile, debugComp.debugData);
        }
    }

    void TerrainService::rebuildModifiedColliders(EntityHandle targetEntity, terrain::TerrainGrid* grid,
                                                   const std::vector<terrain::TileCoord>& modifiedTiles)
    {
        if (modifiedTiles.empty())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(targetEntity);
        if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
        {
            registry.get<components::TerrainComponent>(ent).saveDirty = true;
        }

        if (!physicsProvider || !physicsProvider->hasTerrainCollider(targetEntity))
            return;

        entt::entity terrainEnt = internal::fromHandle(targetEntity);

        for (const auto& coord : modifiedTiles)
        {
            auto* tile = grid->getTile(coord);
            if (tile && tile->hasHeightData())
            {
                std::vector<float> physicsHeights;
                auto info = buildTileColliderInfo(*tile, targetEntity, physicsHeights);
                physicsProvider->rebuildTerrainTileCollider(targetEntity, info);

                if (registry.valid(terrainEnt) &&
                    registry.all_of<components::ChildrenComponent>(terrainEnt))
                {
                    const auto& children = registry.get<components::ChildrenComponent>(terrainEnt).children;
                    for (auto childEnt : children)
                    {
                        if (!registry.valid(childEnt) ||
                            !registry.all_of<components::TerrainTileColliderDebugComponent>(childEnt))
                            continue;

                        auto& debugComp = registry.get<components::TerrainTileColliderDebugComponent>(childEnt);
                        if (debugComp.tileX == coord.x && debugComp.tileZ == coord.z)
                        {
                            generateTileColliderWireframe(*tile, debugComp.debugData);
                            break;
                        }
                    }
                }
            }
        }
    }

    void TerrainService::appendCaveWireframe(const terrain::TerrainTile& tile,
                                              components::TerrainColliderDebugData& out)
    {
        if (!tile.hasCaveGeometry() || tile.caveLOD.isEmpty())
            return;

        uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size());
        glm::vec3 tileOriginOffset(tile.worldOrigin.x, 0.0f, tile.worldOrigin.z);

        for (const auto& v : tile.caveLOD.vertices)
        {
            out.vertices.push_back(v.position + tileOriginOffset);
        }

        for (size_t i = 0; i + 2 < tile.caveLOD.indices.size(); i += 3)
        {
            uint32_t a = baseVertex + tile.caveLOD.indices[i];
            uint32_t b = baseVertex + tile.caveLOD.indices[i + 1];
            uint32_t c = baseVertex + tile.caveLOD.indices[i + 2];
            out.lineIndices.push_back(a);
            out.lineIndices.push_back(b);
            out.lineIndices.push_back(b);
            out.lineIndices.push_back(c);
            out.lineIndices.push_back(c);
            out.lineIndices.push_back(a);
        }
    }

    void TerrainService::appendCaveColliders(EntityHandle terrainEntity,
                                              const std::vector<terrain::TerrainTile*>& allTiles)
    {
        for (auto* tile : allTiles)
        {
            if (!tile || !tile->hasCaveGeometry() || tile->caveLOD.isEmpty())
                continue;

            std::vector<glm::vec3> worldPositions;
            auto caveInfo = buildCaveTileColliderInfo(*tile, worldPositions);
            physicsProvider->addCaveTileCollider(terrainEntity, caveInfo);
        }
    }
}
