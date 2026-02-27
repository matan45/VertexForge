#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/HeightmapLoader.hpp"
#include "resource/ResourceManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/TerrainEvents.hpp"
#include "print/EditorLogger.hpp"

namespace services
{
    EntityHandle TerrainService::createTerrain(const TerrainCreationData& config)
    {
        terrain::TerrainTileConfig tileConfig;

        switch (config.resolution)
        {
        case 0: tileConfig.resolution = terrain::TileResolution::Low; break;
        case 1: tileConfig.resolution = terrain::TileResolution::Medium; break;
        case 2: tileConfig.resolution = terrain::TileResolution::High; break;
        default: tileConfig.resolution = terrain::TileResolution::Low; break;
        }

        tileConfig.worldTileSize = config.worldTileSize;
        tileConfig.maxHeight = config.maxHeight;
        tileConfig.minHeight = config.minHeight;

        for (int i = 0; i < 4; ++i)
        {
            tileConfig.lodDistances[i] = config.lodDistances[i];
        }

        int32_t halfX = config.tilesX / 2;
        int32_t halfZ = config.tilesZ / 2;
        int32_t minX = -halfX;
        int32_t minZ = -halfZ;
        int32_t maxX = config.tilesX - halfX - 1;
        int32_t maxZ = config.tilesZ - halfZ - 1;

        auto grid = std::make_unique<terrain::TerrainGrid>(tileConfig);

        if (config.heightmapPath.empty())
        {
            grid->setHeightSampler([](float /*worldX*/, float /*worldZ*/) -> float {
                return 0.0f;
            });
        }
        else
        {
            auto heightmapData = terrain::HeightmapLoader::load(config.heightmapPath);
            if (heightmapData && heightmapData->isValid())
            {
                float terrainMinX = static_cast<float>(minX) * config.worldTileSize;
                float terrainMinZ = static_cast<float>(minZ) * config.worldTileSize;
                float terrainWidth = static_cast<float>(config.tilesX) * config.worldTileSize;
                float terrainDepth = static_cast<float>(config.tilesZ) * config.worldTileSize;

                grid->setHeightSampler(terrain::createHeightSamplerFromMap(
                    heightmapData,
                    terrainMinX,
                    terrainMinZ,
                    terrainWidth,
                    terrainDepth,
                    config.minHeight,
                    config.maxHeight
                ));

                vfLogInfo("Loaded heightmap from: {}", config.heightmapPath);
            }
            else
            {
                vfLogWarning("Failed to load heightmap: {}, creating flat terrain", config.heightmapPath);
                grid->setHeightSampler([](float /*worldX*/, float /*worldZ*/) -> float {
                    return 0.0f;
                });
            }
        }

        grid->createGrid(minX, minZ, maxX, maxZ, nullptr);

        scene::Entity parentEntity("Terrain");
        sceneGraph->addChild(sceneGraph->GetRoot(), parentEntity);

        auto& terrainComp = parentEntity.addComponent<components::TerrainComponent>();
        terrainComp.resolution = config.resolution;
        terrainComp.worldTileSize = config.worldTileSize;
        terrainComp.maxHeight = config.maxHeight;
        terrainComp.minHeight = config.minHeight;
        terrainComp.gridMinX = minX;
        terrainComp.gridMinZ = minZ;
        terrainComp.gridMaxX = maxX;
        terrainComp.gridMaxZ = maxZ;
        terrainComp.lodDistances = config.lodDistances;
        terrainComp.heightmapPath = config.heightmapPath;
        terrainComp.terrainMaterialPath = config.terrainMaterialPath;
        terrainComp.weightMapPath = config.weightMapPath;
        terrainComp.isActive = true;
        terrainComp.isDirty = false;
        terrainComp.activeTileCount = static_cast<uint32_t>(config.tilesX * config.tilesZ);
        terrainComp.visibleTileCount = 0;

        EntityHandle parentHandle = internal::toHandle(parentEntity.getHandle());

        createTileEntities(parentHandle, *grid);

        terrainGrids[parentHandle.id] = std::move(grid);

        if (!config.weightMapPath.empty())
        {
            loadWeightMaps(parentHandle.id, config.weightMapPath);
        }

        if (!config.terrainMaterialPath.empty())
        {
            syncWeightMapLayerCount(parentHandle.id, config.terrainMaterialPath);
        }

        events::terrain::TerrainCreatedNotification notification;
        notification.terrainEntity = parentHandle;
        notification.config = config;
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("Created terrain with {} tiles", config.tilesX * config.tilesZ);

        return parentHandle;
    }

    void TerrainService::createTileEntities(EntityHandle parentHandle, terrain::TerrainGrid& grid)
    {
        scene::Entity parentEntity(internal::fromHandle(parentHandle));

        for (auto* tile : grid.getAllTiles())
        {
            std::string tileName = "Tile_" + std::to_string(tile->coord.x) + "_" + std::to_string(tile->coord.z);
            scene::Entity tileEntity(tileName);
            parentEntity.addChildren(tileEntity);

            auto& tileComp = tileEntity.addComponent<components::TerrainTileComponent>();
            tileComp.tileX = tile->coord.x;
            tileComp.tileZ = tile->coord.z;
            tileComp.currentLOD = tile->currentLOD;
            tileComp.isVisible = tile->isVisible;
            tileComp.isDirty = tile->isDirty;
            tileComp.isGPUResident = false;
            tileComp.boundingMinY = tile->worldBounds.min.y;
            tileComp.boundingMaxY = tile->worldBounds.max.y;

            auto& transform = tileEntity.getComponent<components::TransformComponent>();
            transform.position = tile->worldOrigin;
            transform.isDirty = true;
        }
    }

    bool TerrainService::deleteTerrain(EntityHandle terrainEntity)
    {
        if (!terrainEntity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity entity = internal::fromHandle(terrainEntity);

        if (!registry.valid(entity))
            return false;

        if (!registry.all_of<components::TerrainComponent>(entity))
            return false;

        const auto& comp = registry.get<components::TerrainComponent>(entity);
        if (!comp.terrainMaterialPath.empty())
            resource::ResourceManager::invalidateTerrainMaterialCache(comp.terrainMaterialPath);

        if (physicsProvider)
            physicsProvider->removeTerrainCollider(terrainEntity);

        terrainGrids.erase(terrainEntity.id);
        fileCaches.erase(terrainEntity.id);

        scene::Entity terrainEnt(entity);
        sceneGraph->removeEntity(terrainEnt);

        events::terrain::TerrainDeletedNotification notification;
        notification.terrainEntity = terrainEntity;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    void TerrainService::remapTerrainEntities()
    {
        if (terrainGrids.empty())
            return;

        std::vector<bool> hadCollider;
        std::vector<std::unique_ptr<terrain::TerrainGrid>> grids;
        std::vector<std::shared_ptr<terrain::TerrainFileCache>> caches;
        for (auto& [id, grid] : terrainGrids)
        {
            bool hasCollider = physicsProvider && physicsProvider->hasTerrainCollider(EntityHandle{id});
            hadCollider.push_back(hasCollider);

            if (hasCollider)
                physicsProvider->removeTerrainCollider(EntityHandle{id});

            grids.push_back(std::move(grid));
            auto cacheIt = fileCaches.find(id);
            caches.push_back(cacheIt != fileCaches.end() ? std::move(cacheIt->second) : nullptr);
        }
        terrainGrids.clear();
        fileCaches.clear();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::TerrainComponent>();

        size_t gridIndex = 0;
        for (auto entity : view)
        {
            if (gridIndex >= grids.size())
                break;

            uint64_t newId = internal::toHandle(entity).id;
            terrainGrids[newId] = std::move(grids[gridIndex]);
            if (caches[gridIndex])
                fileCaches[newId] = std::move(caches[gridIndex]);

            if (gridIndex < hadCollider.size() && hadCollider[gridIndex])
                addTerrainCollider(EntityHandle{newId});

            gridIndex++;
        }
    }

    std::vector<terrain::TerrainTile*> TerrainService::getRawVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<terrain::TerrainTile*> result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            (void)grid->updateLODs(cameraPosition);

            grid->regenerateDirtyTiles(cameraPosition);

            auto visibleTiles = grid->getVisibleTiles(frustum);

            if (distanceCullingEnabled_ && maxTerrainDistSq_ > 0.0f)
            {
                for (terrain::TerrainTile* tile : visibleTiles)
                {
                    if (!tile || !tile->isVisible)
                        continue;

                    glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
                    glm::vec3 diff = tileCenter - cameraPosition;
                    float distSq = glm::dot(diff, diff);
                    if (distSq <= maxTerrainDistSq_)
                    {
                        result.push_back(tile);
                    }
                }
            }
            else
            {
                for (terrain::TerrainTile* tile : visibleTiles)
                {
                    if (tile && tile->isVisible)
                    {
                        result.push_back(tile);
                    }
                }
            }
        }

        return result;
    }

    std::vector<terrain::TerrainTile*> TerrainService::queryVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<terrain::TerrainTile*> result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            for (auto* tile : grid->getAllTiles())
            {
                if (!tile)
                    continue;

                if (!frustum.intersectsAABB(tile->worldBounds))
                    continue;

                if (distanceCullingEnabled_ && maxTerrainDistSq_ > 0.0f)
                {
                    glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
                    glm::vec3 diff = tileCenter - cameraPosition;
                    float distSq = glm::dot(diff, diff);
                    if (distSq > maxTerrainDistSq_)
                        continue;
                }

                result.push_back(tile);
            }
        }

        return result;
    }

    bool TerrainService::ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel)
    {
        for (auto& [entityId, grid] : terrainGrids)
        {
            if (!grid->getTile(tile.coord))
                continue;

            auto cacheIt = fileCaches.find(entityId);
            if (cacheIt == fileCaches.end() || !cacheIt->second)
                return false;

            auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
                return grid->getTile(coord);
            };

            return cacheIt->second->ensureLODsLoaded(tile, grid->getGenerator(), getTile);
        }

        return false;
    }

    void TerrainService::releaseTileRAMData(terrain::TerrainTile& tile)
    {
        for (auto& [entityId, grid] : terrainGrids)
        {
            if (!grid->getTile(tile.coord))
                continue;

            auto cacheIt = fileCaches.find(entityId);
            if (cacheIt != fileCaches.end() && cacheIt->second)
            {
                cacheIt->second->evictTileGeometry(tile);
            }
            return;
        }
    }
}
