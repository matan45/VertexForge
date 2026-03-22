#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainTile.hpp"
#include "vegetation/VegetationSerializer.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include <algorithm>
#include <filesystem>
#include <format>

namespace services
{
    bool TerrainService::addTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ)
    {
        if (!terrainEntity.isValid())
            return false;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return false;

        auto& grid = *gridIt->second;
        terrain::TileCoord coord{tileX, tileZ};

        // Check if tile already exists
        if (grid.getTile(coord))
            return false;

        terrain::TerrainTile* tile = grid.addTile(coord);
        if (!tile)
            return false;

        // Track in file cache if one exists
        auto cacheIt = fileCaches.find(terrainEntity.id);
        if (cacheIt != fileCaches.end() && cacheIt->second)
        {
            cacheIt->second->addNewTileEntry(coord);
        }

        createTileEntity(terrainEntity, tile, tileX, tileZ);

        // Update TerrainComponent bounds
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(terrainEntity);
        if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
        {
            auto& comp = registry.get<components::TerrainComponent>(ent);
            int32_t minX, minZ, maxX, maxZ;
            grid.computeBounds(minX, minZ, maxX, maxZ);
            comp.gridMinX = minX;
            comp.gridMinZ = minZ;
            comp.gridMaxX = maxX;
            comp.gridMaxZ = maxZ;
            comp.activeTileCount = static_cast<uint32_t>(grid.getTileCount());
            comp.saveDirty = true;
        }

        // Create physics body for the new tile if collider is active
        if (physicsProvider && physicsProvider->hasTerrainCollider(terrainEntity)
            && tile->hasHeightData())
        {
            std::vector<float> physicsHeights;
            auto info = buildTileColliderInfo(*tile, terrainEntity, physicsHeights);
            physicsProvider->addTerrainTileCollider(terrainEntity, info);
        }

        events::terrain::TerrainTileAddedNotification notification;
        notification.terrainEntity = terrainEntity;
        notification.tileX = tileX;
        notification.tileZ = tileZ;
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("TerrainService: Added tile ({}, {})", tileX, tileZ);
        return true;
    }

    bool TerrainService::removeTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ)
    {
        if (!terrainEntity.isValid())
            return false;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return false;

        auto& grid = *gridIt->second;
        terrain::TileCoord coord{tileX, tileZ};

        if (!grid.getTile(coord))
            return false;

        // Remove single-tile physics body if collider is active
        if (physicsProvider && physicsProvider->hasTerrainCollider(terrainEntity))
        {
            physicsProvider->removeTerrainTileCollider(terrainEntity, tileX, tileZ);
            physicsProvider->removeCaveTileCollider(terrainEntity, tileX, tileZ);
        }

        // Remove from grid (clears neighbor refs, marks neighbors dirty)
        grid.removeTile(coord);

        // Remove from file cache
        auto cacheIt = fileCaches.find(terrainEntity.id);
        if (cacheIt != fileCaches.end() && cacheIt->second)
        {
            cacheIt->second->removeEntry(coord);
        }

        // Find and destroy the child tile entity
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity parentEnt = internal::fromHandle(terrainEntity);
        if (registry.valid(parentEnt) && registry.all_of<components::ChildrenComponent>(parentEnt))
        {
            const auto& children = registry.get<components::ChildrenComponent>(parentEnt).children;
            for (auto childEnt : children)
            {
                if (!registry.valid(childEnt) ||
                    !registry.all_of<components::TerrainTileComponent>(childEnt))
                    continue;

                const auto& tc = registry.get<components::TerrainTileComponent>(childEnt);
                if (tc.tileX == tileX && tc.tileZ == tileZ)
                {
                    scene::Entity tileEntity(childEnt);
                    sceneGraph->removeEntity(tileEntity);
                    break;
                }
            }
        }

        // Update TerrainComponent bounds
        if (registry.valid(parentEnt) && registry.all_of<components::TerrainComponent>(parentEnt))
        {
            auto& comp = registry.get<components::TerrainComponent>(parentEnt);
            int32_t minX, minZ, maxX, maxZ;
            grid.computeBounds(minX, minZ, maxX, maxZ);
            comp.gridMinX = minX;
            comp.gridMinZ = minZ;
            comp.gridMaxX = maxX;
            comp.gridMaxZ = maxZ;
            comp.activeTileCount = static_cast<uint32_t>(grid.getTileCount());
            comp.saveDirty = true;
        }

        events::terrain::TerrainTileRemovedNotification notification;
        notification.terrainEntity = terrainEntity;
        notification.tileX = tileX;
        notification.tileZ = tileZ;
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("TerrainService: Removed tile ({}, {})", tileX, tileZ);
        return true;
    }

    bool TerrainService::streamInTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ)
    {
        if (!terrainEntity.isValid())
            return false;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return false;

        auto& grid = *gridIt->second;
        terrain::TileCoord coord{tileX, tileZ};

        if (grid.getTile(coord))
            return false;

        terrain::TerrainTile* tile = grid.addTileFromFile(coord);
        if (!tile)
            return false;

        // Note: saveDirty is intentionally NOT set here. Streamed-in tiles are
        // transient copies loaded from an already-saved file, not user modifications.

        // Restore vegetation data for the streamed-in tile
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(terrainEntity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                const auto& tc = registry.get<components::TerrainComponent>(ent);
                if (!tc.savePath.empty())
                {
                    namespace fs = std::filesystem;
                    std::string vegDir = getVegetationDirectory(tc.savePath);

                    for (uint32_t t = 0; t < vegetation::VEGETATION_TYPE_COUNT; ++t)
                    {
                        std::string densityPath = std::format("{}/tile_{}_{}_{}.vfVegDensity",
                            vegDir, tileX, tileZ, t);
                        if (fs::exists(densityPath))
                        {
                            vegetation::VegetationSerializer::loadDensityMap(densityPath, tile->vegetationDensityMaps[t]);
                            tile->vegetationDensityDirty[t] = true;
                            tile->vegetationDensityGPUDirty[t] = true;
                        }
                    }

                }
            }
        }

        createTileEntity(terrainEntity, tile, tileX, tileZ);

        // Queue physics body creation for after height data is loaded
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity parentEnt = internal::fromHandle(terrainEntity);
        if (physicsProvider && registry.valid(parentEnt) &&
            registry.all_of<components::TerrainColliderComponent>(parentEnt))
        {
            const auto& cc = registry.get<components::TerrainColliderComponent>(parentEnt);
            if (cc.hasCollider)
                pendingPhysicsTiles.push_back({terrainEntity.id, {tileX, tileZ}});
        }

        events::terrain::TerrainTileAddedNotification notification;
        notification.terrainEntity = terrainEntity;
        notification.tileX = tileX;
        notification.tileZ = tileZ;
        notification.isStreamed = true;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    bool TerrainService::streamOutTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ)
    {
        if (!terrainEntity.isValid())
            return false;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return false;

        auto& grid = *gridIt->second;
        terrain::TileCoord coord{tileX, tileZ};

        if (!grid.getTile(coord))
            return false;

        // Safety net: refuse to unload tiles with unsaved brush modifications
        auto cacheIt = fileCaches.find(terrainEntity.id);
        if (cacheIt != fileCaches.end() && cacheIt->second
            && cacheIt->second->isTileDirty(coord))
        {
            vfLogWarning("streamOutTile: refusing to unload dirty tile ({}, {})", tileX, tileZ);
            return false;
        }

        // Remove physics bodies for this tile (surface + cave)
        if (physicsProvider && physicsProvider->hasTerrainCollider(terrainEntity))
        {
            physicsProvider->removeTerrainTileCollider(terrainEntity, tileX, tileZ);
            physicsProvider->removeCaveTileCollider(terrainEntity, tileX, tileZ);
        }

        // Remove from pending queue if it was waiting for height data
        pendingPhysicsTiles.erase(
            std::remove_if(pendingPhysicsTiles.begin(), pendingPhysicsTiles.end(),
                [&](const auto& p) {
                    return p.first == terrainEntity.id && p.second.x == tileX && p.second.z == tileZ;
                }),
            pendingPhysicsTiles.end());

        grid.removeTile(coord);

        // Note: saveDirty is intentionally NOT set here. Streaming out a tile
        // does not alter the saved file — the tile data remains on disk.

        // Find and destroy the child tile entity
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity parentEnt = internal::fromHandle(terrainEntity);
        if (registry.valid(parentEnt) && registry.all_of<components::ChildrenComponent>(parentEnt))
        {
            const auto& children = registry.get<components::ChildrenComponent>(parentEnt).children;
            for (auto childEnt : children)
            {
                if (!registry.valid(childEnt) ||
                    !registry.all_of<components::TerrainTileComponent>(childEnt))
                    continue;

                const auto& tc = registry.get<components::TerrainTileComponent>(childEnt);
                if (tc.tileX == tileX && tc.tileZ == tileZ)
                {
                    scene::Entity tileEntity(childEnt);
                    sceneGraph->removeEntity(tileEntity);
                    break;
                }
            }
        }

        events::terrain::TerrainTileRemovedNotification notification;
        notification.terrainEntity = terrainEntity;
        notification.tileX = tileX;
        notification.tileZ = tileZ;
        notification.isStreamed = true;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    void TerrainService::commitStreamingChanges(EntityHandle terrainEntity)
    {
        if (!terrainEntity.isValid())
            return;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(terrainEntity);
        if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
        {
            auto& comp = registry.get<components::TerrainComponent>(ent);
            int32_t minX, minZ, maxX, maxZ;
            gridIt->second->computeBounds(minX, minZ, maxX, maxZ);
            comp.gridMinX = minX;
            comp.gridMinZ = minZ;
            comp.gridMaxX = maxX;
            comp.gridMaxZ = maxZ;
            comp.activeTileCount = static_cast<uint32_t>(gridIt->second->getTileCount());
        }
    }

    void TerrainService::loadAllTiles(EntityHandle terrainEntity)
    {
        if (!terrainEntity.isValid())
            return;

        auto cacheIt = fileCaches.find(terrainEntity.id);
        if (cacheIt == fileCaches.end() || !cacheIt->second)
            return;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end())
            return;

        auto& grid = *gridIt->second;
        auto& fileCache = *cacheIt->second;

        // Disable streaming so tiles won't be unloaded again
        auto streamerIt = worldStreamers.find(terrainEntity.id);
        if (streamerIt != worldStreamers.end() && streamerIt->second)
        {
            streamerIt->second->setEnabled(false);
        }

        fileCache.forEachSavedCoord([&](const terrain::TileCoord& coord)
        {
            if (!grid.hasTile(coord))
            {
                streamInTile(terrainEntity, coord.x, coord.z);
            }
        });

        commitStreamingChanges(terrainEntity);
    }
}
