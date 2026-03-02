#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainWeightMapAsset.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "resource/ResourceManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/TerrainEvents.hpp"
#include "print/EditorLogger.hpp"
#include <cfloat>

namespace
{
    void generateTileColliderWireframe(
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

        uint32_t lineCount = vertexCount * (vertexCount - 1) * 2;
        out.lineIndices.clear();
        out.lineIndices.reserve(lineCount * 2);

        for (uint32_t z = 0; z < vertexCount; ++z)
        {
            for (uint32_t x = 0; x < vertexCount - 1; ++x)
            {
                out.lineIndices.push_back(z * vertexCount + x);
                out.lineIndices.push_back(z * vertexCount + x + 1);
            }
        }
        for (uint32_t x = 0; x < vertexCount; ++x)
        {
            for (uint32_t z = 0; z < vertexCount - 1; ++z)
            {
                out.lineIndices.push_back(z * vertexCount + x);
                out.lineIndices.push_back((z + 1) * vertexCount + x);
            }
        }

        out.version++;
    }
}

namespace services
{
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

        std::vector<TerrainTileColliderInfo> tileInfos;
        tileInfos.reserve(allTiles.size());

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

            TerrainTileColliderInfo info;
            info.tileX = tile->coord.x;
            info.tileZ = tile->coord.z;
            info.heightSamples = tile->heightData.data();
            info.sampleCount = tile->config.getVertexCount();
            info.worldOrigin = tile->worldOrigin;
            info.vertexSpacing = tile->config.getVertexSpacing();
            info.friction = friction;
            info.restitution = restitution;
            info.collisionLayer = collisionLayer;

            tileInfos.push_back(info);
        }

        if (tileInfos.empty())
            return false;

        physicsProvider->addTerrainCollider(terrainEntity, tileInfos);

        if (registry.valid(ent))
        {
            if (!registry.all_of<components::TerrainColliderComponent>(ent))
            {
                registry.emplace<components::TerrainColliderComponent>(ent);
            }
            registry.get<components::TerrainColliderComponent>(ent).hasCollider = true;
        }

        generateDebugWireframes(terrainEntity, grid);

        vfLogInfo("TerrainService: Added terrain collider with {} tiles", tileInfos.size());
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
                // If tile has holes, create temp height array with FLT_MAX for hole vertices
                // so Jolt's HeightFieldShape excludes those triangles from collision
                std::vector<float> physicsHeights;
                const float* heightSamples = tile->heightData.data();

                if (tile->hasHoleMask())
                {
                    bool hasAnyHole = false;
                    for (uint8_t h : tile->holeMask)
                    {
                        if (h) { hasAnyHole = true; break; }
                    }

                    if (hasAnyHole)
                    {
                        physicsHeights = tile->heightData;
                        for (size_t i = 0; i < physicsHeights.size(); ++i)
                        {
                            if (tile->holeMask[i])
                                physicsHeights[i] = FLT_MAX;
                        }
                        heightSamples = physicsHeights.data();
                    }
                }

                TerrainTileColliderInfo info;
                info.tileX = coord.x;
                info.tileZ = coord.z;
                info.heightSamples = heightSamples;
                info.sampleCount = tile->config.getVertexCount();
                info.worldOrigin = tile->worldOrigin;
                info.vertexSpacing = tile->config.getVertexSpacing();
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

    bool TerrainService::saveWeightMaps(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        auto allTiles = gridIt->second->getAllTiles();
        if (allTiles.empty())
        {
            vfLogWarning("TerrainService: No tiles to save weight maps for");
            return true;
        }

        uint32_t resolution = allTiles[0]->config.getVertexCount();

        std::string materialPath;
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});
        if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
        {
            materialPath = registry.get<components::TerrainComponent>(ent).terrainMaterialPath;
        }

        std::unordered_map<terrain::TileCoord, terrain::TileWeightMapData, terrain::TileCoordHash> tileWeights;
        for (const auto* tile : allTiles)
        {
            if (tile->hasWeightMap())
            {
                tileWeights.emplace(tile->coord, tile->weightMap);
            }
        }

        return terrain::TerrainWeightMapAsset::save(path, tileWeights, resolution, materialPath);
    }

    bool TerrainService::loadWeightMaps(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        std::string materialPath;
        auto loadedWeights = terrain::TerrainWeightMapAsset::load(path, &materialPath);
        if (loadedWeights.empty())
        {
            return false;
        }

        if (!materialPath.empty())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).terrainMaterialPath = materialPath;
                syncWeightMapLayerCount(terrainEntityId, materialPath);
            }
        }

        auto allTiles = gridIt->second->getAllTiles();
        uint32_t loadedCount = 0;

        for (auto* tile : allTiles)
        {
            auto it = loadedWeights.find(tile->coord);
            if (it != loadedWeights.end())
            {
                if (it->second.resolution == tile->config.getVertexCount())
                {
                    tile->weightMap = std::move(it->second);
                    tile->weightMapDirty = true;
                    tile->weightMapGPUDirty = true;
                    loadedCount++;
                }
                else
                {
                    vfLogWarning("TerrainService: Weight map resolution mismatch for tile ({}, {}): "
                                 "expected {}, got {}",
                                 tile->coord.x, tile->coord.z,
                                 tile->config.getVertexCount(), it->second.resolution);
                }
            }
        }

        vfLogInfo("TerrainService: Loaded weight maps for {} of {} tiles", loadedCount, allTiles.size());
        return true;
    }

    bool TerrainService::saveTerrain(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});

        if (!registry.valid(ent) || !registry.all_of<components::TerrainComponent>(ent))
        {
            vfLogError("TerrainService: Entity {} has no TerrainComponent", terrainEntityId);
            return false;
        }

        const auto& comp = registry.get<components::TerrainComponent>(ent);

        terrain::TerrainTileConfig tileConfig;
        switch (comp.resolution)
        {
        case 0: tileConfig.resolution = terrain::TileResolution::Low; break;
        case 1: tileConfig.resolution = terrain::TileResolution::Medium; break;
        case 2: tileConfig.resolution = terrain::TileResolution::High; break;
        default: tileConfig.resolution = terrain::TileResolution::Low; break;
        }
        tileConfig.worldTileSize = comp.worldTileSize;
        tileConfig.maxHeight = comp.maxHeight;
        tileConfig.minHeight = comp.minHeight;
        for (int i = 0; i < 4; ++i)
            tileConfig.lodDistances[i] = comp.lodDistances[i];

        auto cacheIt = fileCaches.find(terrainEntityId);
        if (cacheIt != fileCaches.end() && cacheIt->second)
        {
            auto& grid = *gridIt->second;
            auto& generator = grid.getGenerator();
            auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
                return grid.getTile(coord);
            };

            for (auto* tile : grid.getAllTiles())
            {
                if (tile && !tile->hasHeightData())
                    cacheIt->second->ensureHeightsLoaded(*tile);
                if (tile && !tile->hasAnyLODData())
                    cacheIt->second->ensureLODsLoaded(*tile, generator, getTile);
            }
        }

        terrain::TerrainPhysicsConfig physicsConfig;
        if (registry.all_of<components::TerrainColliderComponent>(ent))
        {
            const auto& cc = registry.get<components::TerrainColliderComponent>(ent);
            physicsConfig.hasCollider = cc.hasCollider;
            physicsConfig.collisionLayer = cc.collisionLayer;
            physicsConfig.friction = cc.friction;
            physicsConfig.restitution = cc.restitution;
        }

        bool result = terrain::TerrainSerializer::save(
            path, *gridIt->second, tileConfig,
            comp.gridMinX, comp.gridMinZ, comp.gridMaxX, comp.gridMaxZ,
            comp.terrainMaterialPath, physicsConfig);

        if (result)
        {
            auto& mutableComp = registry.get<components::TerrainComponent>(ent);
            mutableComp.savePath = path;
            mutableComp.saveDirty = false;

            auto cacheIt = fileCaches.find(terrainEntityId);
            if (cacheIt != fileCaches.end() && cacheIt->second)
            {
                cacheIt->second->refreshIndex(path);
            }
            else
            {
                terrain::TerrainFileHeader newHeader;
                std::vector<terrain::TileIndexEntry> newIndex;
                if (terrain::TerrainSerializer::readHeader(path, newHeader, newIndex))
                {
                    auto cache = std::make_shared<terrain::TerrainFileCache>(path, newHeader, newIndex);
                    fileCaches[terrainEntityId] = cache;
                    gridIt->second->setFileCache(cache);
                }
            }

            events::terrain::TerrainSavedNotification savedNotification;
            savedNotification.terrainEntity = EntityHandle{terrainEntityId};
            savedNotification.path = path;
            events::EventDispatcher::instance().publish(savedNotification);

            vfLogInfo("TerrainService: Saved terrain to {}", path);
        }

        return result;
    }

    EntityHandle TerrainService::loadTerrain(const std::string& path)
    {
        terrain::TerrainFileHeader header;
        std::vector<terrain::TileIndexEntry> index;

        if (!terrain::TerrainSerializer::readHeader(path, header, index))
        {
            vfLogError("TerrainService: Failed to read terrain header from {}", path);
            return {};
        }

        return finishLoadTerrain(header, index, path);
    }

    EntityHandle TerrainService::finishLoadTerrain(
        terrain::TerrainFileHeader& header,
        std::vector<terrain::TileIndexEntry>& index,
        const std::string& path)
    {
        terrain::TerrainTileConfig tileConfig;
        tileConfig.resolution = static_cast<terrain::TileResolution>(header.resolution);
        tileConfig.worldTileSize = header.worldTileSize;
        tileConfig.maxHeight = header.maxHeight;
        tileConfig.minHeight = header.minHeight;
        tileConfig.lodDistances = header.lodDistances;
        tileConfig.skirtDepth = header.skirtDepth;

        auto grid = std::make_unique<terrain::TerrainGrid>(tileConfig);
        grid->loadMetadataOnly(header, index);

        auto cache = std::make_shared<terrain::TerrainFileCache>(path, header, index);
        grid->setFileCache(cache);

        scene::Entity parentEntity("Terrain");
        sceneGraph->addChild(sceneGraph->GetRoot(), parentEntity);

        auto& terrainComp = parentEntity.addComponent<components::TerrainComponent>();
        terrainComp.resolution = header.resolution;
        terrainComp.worldTileSize = header.worldTileSize;
        terrainComp.maxHeight = header.maxHeight;
        terrainComp.minHeight = header.minHeight;
        terrainComp.gridMinX = header.gridMinX;
        terrainComp.gridMinZ = header.gridMinZ;
        terrainComp.gridMaxX = header.gridMaxX;
        terrainComp.gridMaxZ = header.gridMaxZ;
        terrainComp.lodDistances = header.lodDistances;
        terrainComp.terrainMaterialPath = header.materialPath;
        terrainComp.isActive = true;
        terrainComp.isDirty = false;
        terrainComp.activeTileCount = header.tileCount;
        terrainComp.visibleTileCount = 0;
        terrainComp.savePath = path;
        terrainComp.saveDirty = false;

        EntityHandle parentHandle = internal::toHandle(parentEntity.getHandle());

        createTileEntities(parentHandle, *grid);

        terrainGrids[parentHandle.id] = std::move(grid);
        fileCaches[parentHandle.id] = cache;

        if (!header.materialPath.empty())
        {
            syncWeightMapLayerCount(parentHandle.id, header.materialPath);
        }

        events::terrain::TerrainCreatedNotification notification;
        notification.terrainEntity = parentHandle;
        notification.config.resolution = header.resolution;
        notification.config.worldTileSize = header.worldTileSize;
        notification.config.maxHeight = header.maxHeight;
        notification.config.minHeight = header.minHeight;
        notification.config.terrainMaterialPath = header.materialPath;
        notification.config.tilesX = header.gridMaxX - header.gridMinX + 1;
        notification.config.tilesZ = header.gridMaxZ - header.gridMinZ + 1;
        for (int i = 0; i < 4; ++i)
            notification.config.lodDistances[i] = header.lodDistances[i];
        events::EventDispatcher::instance().publish(notification);

        if (header.physicsConfig.hasCollider && physicsProvider)
        {
            auto& cc = parentEntity.addComponent<components::TerrainColliderComponent>();
            cc.collisionLayer = header.physicsConfig.collisionLayer;
            cc.friction = header.physicsConfig.friction;
            cc.restitution = header.physicsConfig.restitution;
            addTerrainCollider(parentHandle);
        }

        vfLogInfo("TerrainService: Loaded terrain with {} tiles from {}", header.tileCount, path);

        return parentHandle;
    }
}
