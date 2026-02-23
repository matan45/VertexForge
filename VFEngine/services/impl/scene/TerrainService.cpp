#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "resource/ResourceManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/TerrainEvents.hpp"
#include "../../events/BrushEvents.hpp"
#include "../../events/PaintBrushEvents.hpp"
#include "../../events/SceneEvents.hpp"
#include "../../events/PhysicsEvents.hpp"
#include "print/EditorLogger.hpp"
#include <algorithm>
#include <cstring>

namespace services
{
    TerrainService::TerrainService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
    {
    }

    TerrainService::~TerrainService()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<events::terrain::CreateTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::DeleteTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::RemapTerrainEntitiesCommand>();
        dispatcher.unregisterCommandHandler<events::brush::ApplyBrushCommand>();
        dispatcher.unregisterCommandHandler<events::paintBrush::ApplyPaintBrushCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SetTerrainMaterialPathCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SaveWeightMapsCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::LoadWeightMapsCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SaveTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::LoadTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SetTerrainSaveLockCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::BeginTerrainLoadCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::PollTerrainLoadCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SetTerrainColliderPropertiesCommand>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainDataQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::HasTerrainComponentQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::HasTerrainTileComponentQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainTileDataQuery>();

        if (entityDeletedSubscription && entityDeletedSubscription->isValid())
        {
            dispatcher.unsubscribe(*entityDeletedSubscription);
        }

        if (sceneClearedSubscription && sceneClearedSubscription->isValid())
        {
            dispatcher.unsubscribe(*sceneClearedSubscription);
        }

        terrainGrids.clear();
    }

    std::optional<TerrainData> TerrainService::getTerrainData(EntityHandle entity) const
    {
        if (!entity.isValid())
            return std::nullopt;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return std::nullopt;

        if (!registry.all_of<components::TerrainComponent>(ent))
            return std::nullopt;

        const auto& comp = registry.get<components::TerrainComponent>(ent);

        TerrainData data;
        data.resolution = comp.resolution;
        data.worldTileSize = comp.worldTileSize;
        data.maxHeight = comp.maxHeight;
        data.minHeight = comp.minHeight;
        data.gridMinX = comp.gridMinX;
        data.gridMinZ = comp.gridMinZ;
        data.gridMaxX = comp.gridMaxX;
        data.gridMaxZ = comp.gridMaxZ;
        data.heightmapPath = comp.heightmapPath;
        data.terrainMaterialPath = comp.terrainMaterialPath;
        data.weightMapPath = comp.weightMapPath;
        data.tileCount = static_cast<uint32_t>((comp.gridMaxX - comp.gridMinX + 1) *
                                                (comp.gridMaxZ - comp.gridMinZ + 1));
        data.isActive = comp.isActive;
        data.isDirty = comp.isDirty;
        data.activeTileCount = comp.activeTileCount;
        data.visibleTileCount = comp.visibleTileCount;
        data.savePath = comp.savePath;
        data.saveDirty = comp.saveDirty;

        if (registry.all_of<components::TerrainColliderComponent>(ent))
        {
            const auto& cc = registry.get<components::TerrainColliderComponent>(ent);
            data.colliderCollisionLayer = cc.collisionLayer;
            data.colliderFriction = cc.friction;
            data.colliderRestitution = cc.restitution;
        }

        return data;
    }

    bool TerrainService::hasTerrainComponent(EntityHandle entity) const
    {
        if (!entity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return false;

        return registry.all_of<components::TerrainComponent>(ent);
    }

    std::string TerrainService::getTerrainMaterialPath() const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::TerrainComponent>();
        for (auto entity : view)
        {
            const auto& comp = view.get<components::TerrainComponent>(entity);
            if (!comp.terrainMaterialPath.empty())
            {
                return comp.terrainMaterialPath;
            }
        }
        return {};
    }

    bool TerrainService::hasTerrainTileComponent(EntityHandle entity) const
    {
        if (!entity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return false;

        return registry.all_of<components::TerrainTileComponent>(ent);
    }

    std::optional<TerrainTileData> TerrainService::getTerrainTileData(EntityHandle entity) const
    {
        if (!entity.isValid())
            return std::nullopt;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return std::nullopt;

        if (!registry.all_of<components::TerrainTileComponent>(ent))
            return std::nullopt;

        const auto& comp = registry.get<components::TerrainTileComponent>(ent);

        TerrainTileData data;
        data.tileX = comp.tileX;
        data.tileZ = comp.tileZ;
        data.currentLOD = comp.currentLOD;
        data.isVisible = comp.isVisible;
        data.isDirty = comp.isDirty;
        data.isGPUResident = comp.isGPUResident;
        data.boundingMinY = comp.boundingMinY;
        data.boundingMaxY = comp.boundingMaxY;

        return data;
    }

    void TerrainService::onEntityDeleted(EntityHandle entity)
    {
        if (!entity.isValid())
            return;

        auto it = terrainGrids.find(entity.id);
        if (it != terrainGrids.end())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(entity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                const auto& comp = registry.get<components::TerrainComponent>(ent);
                if (!comp.terrainMaterialPath.empty())
                    resource::ResourceManager::invalidateTerrainMaterialCache(comp.terrainMaterialPath);
            }

            if (physicsProvider)
                physicsProvider->removeTerrainCollider(entity);

            terrainGrids.erase(it);
            fileCaches.erase(entity.id);

            events::terrain::TerrainDeletedNotification notification;
            notification.terrainEntity = entity;
            events::EventDispatcher::instance().publish(notification);
        }
    }

    void TerrainService::onSceneCleared()
    {
        if (terrainGrids.empty())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto& [id, _] : terrainGrids)
        {
            if (physicsProvider)
                physicsProvider->removeTerrainCollider(EntityHandle{id});

            entt::entity ent = internal::fromHandle(EntityHandle{id});
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                const auto& comp = registry.get<components::TerrainComponent>(ent);
                if (!comp.terrainMaterialPath.empty())
                    resource::ResourceManager::invalidateTerrainMaterialCache(comp.terrainMaterialPath);
            }
        }

        events::terrain::TerrainDeletedNotification notification;
        events::EventDispatcher::instance().publish(notification);

        terrainGrids.clear();
        fileCaches.clear();

        vfLogInfo("TerrainService: Cleared all terrains on scene clear");
    }

    void TerrainService::syncWeightMapLayerCount(uint64_t terrainEntityId, const std::string& materialPath)
    {
        if (materialPath.empty())
        {
            return;
        }

        auto materialData = resource::ResourceManager::loadTerrainMaterial(materialPath);
        if (!materialData)
        {
            return;
        }

        uint8_t layerCount = materialData->activeLayerCount;
        if (layerCount == 0)
        {
            layerCount = 1;
        }

        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            return;
        }

        terrain::TerrainGrid* grid = gridIt->second.get();
        grid->updateWeightMapLayerCount(layerCount);
    }

    events::terrain::TerrainGeometryResult TerrainService::getTerrainGeometryForNavmesh()
    {
        events::terrain::TerrainGeometryResult result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            auto& generator = grid->getGenerator();
            auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
                return grid->getTile(coord);
            };

            auto allTiles = grid->getAllTiles();
            for (auto* tile : allTiles)
            {
                if (!tile || !tile->hasHeightData())
                    continue;

                if (!tile->hasLODData(0))
                {
                    generator.regenerateLOD(*tile, 0, getTile);
                }

                const auto& lod0 = tile->lodLevels[0];
                int baseVertex = static_cast<int>(result.vertices.size() / 3);

                // Use only X/Z from worldOrigin (Y=0) to match GPU terrain rendering,
                // which translates by (worldOrigin.x, 0, worldOrigin.z)
                const glm::vec3 origin(tile->worldOrigin.x, 0.0f, tile->worldOrigin.z);
                for (const auto& vertex : lod0.vertices)
                {
                    glm::vec3 worldPos = origin + vertex.position;
                    result.vertices.push_back(worldPos.x);
                    result.vertices.push_back(worldPos.y);
                    result.vertices.push_back(worldPos.z);

                    if (result.vertices.size() == 3)
                    {
                        result.boundsMin = worldPos;
                        result.boundsMax = worldPos;
                    }
                    else
                    {
                        result.boundsMin = glm::min(result.boundsMin, worldPos);
                        result.boundsMax = glm::max(result.boundsMax, worldPos);
                    }
                }

                for (size_t i = 0; i < lod0.indices.size(); i += 3)
                {
                    result.triangles.push_back(baseVertex + static_cast<int>(lod0.indices[i]));
                    result.triangles.push_back(baseVertex + static_cast<int>(lod0.indices[i + 1]));
                    result.triangles.push_back(baseVertex + static_cast<int>(lod0.indices[i + 2]));
                }
            }
        }

        return result;
    }

    events::terrain::TerrainHeightfieldResult TerrainService::getTerrainHeightfield()
    {
        events::terrain::TerrainHeightfieldResult result;

        if (terrainGrids.empty())
            return result;

        // Use the first terrain grid
        auto& [entityId, grid] = *terrainGrids.begin();
        auto allTiles = grid->getAllTiles();

        if (allTiles.empty())
            return result;

        // Determine grid bounds from tile coordinates
        int32_t minTileX = allTiles[0]->coord.x;
        int32_t maxTileX = allTiles[0]->coord.x;
        int32_t minTileZ = allTiles[0]->coord.z;
        int32_t maxTileZ = allTiles[0]->coord.z;

        for (auto* tile : allTiles)
        {
            if (!tile) continue;
            minTileX = std::min(minTileX, tile->coord.x);
            maxTileX = std::max(maxTileX, tile->coord.x);
            minTileZ = std::min(minTileZ, tile->coord.z);
            maxTileZ = std::max(maxTileZ, tile->coord.z);
        }

        const auto& config = allTiles[0]->config;
        int32_t gridCountX = maxTileX - minTileX + 1;
        int32_t gridCountZ = maxTileZ - minTileZ + 1;
        uint32_t vpt = config.getQuadCount() + 1;  // vertices per side (33, 65, or 129)

        result.worldOriginX = static_cast<float>(minTileX) * config.worldTileSize;
        result.worldOriginZ = static_cast<float>(minTileZ) * config.worldTileSize;
        result.tileWorldSize = config.worldTileSize;
        result.vertexSpacing = config.getVertexSpacing();
        result.gridCountX = gridCountX;
        result.gridCountZ = gridCountZ;
        result.verticesPerTile = vpt;

        // Allocate heights buffer (zeroed)
        size_t totalHeights = static_cast<size_t>(gridCountX) * gridCountZ * vpt * vpt;
        result.heights.resize(totalHeights, 0.0f);

        // Pack each tile's heightData into the correct grid position
        for (auto* tile : allTiles)
        {
            if (!tile || !tile->hasHeightData())
                continue;

            int32_t tileOffsetX = tile->coord.x - minTileX;
            int32_t tileOffsetZ = tile->coord.z - minTileZ;
            uint32_t tileIndex = static_cast<uint32_t>(tileOffsetZ) * static_cast<uint32_t>(gridCountX)
                               + static_cast<uint32_t>(tileOffsetX);
            size_t baseOffset = static_cast<size_t>(tileIndex) * vpt * vpt;

            size_t copyCount = std::min(tile->heightData.size(),
                                        static_cast<size_t>(vpt * vpt));
            std::memcpy(result.heights.data() + baseOffset,
                        tile->heightData.data(),
                        copyCount * sizeof(float));
        }

        result.valid = true;
        return result;
    }
}
