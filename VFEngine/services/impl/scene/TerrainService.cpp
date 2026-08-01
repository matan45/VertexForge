#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "terrain/TileHeightSampler.hpp"
#include "resource/ResourceManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/PaintBrushEvents.hpp"
#include "../../events/terrain/HoleBrushEvents.hpp"
#include "../../events/terrain/TerrainStrokeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
    bool resolveTerrainSamplePosition(float worldX, float worldZ, float worldTileSize,
                                      terrain::TileCoord& coord, float& localX, float& localZ)
    {
        if (!std::isfinite(worldX) || !std::isfinite(worldZ)
            || !std::isfinite(worldTileSize) || worldTileSize <= 0.0f)
            return false;

        const double tileX = std::floor(static_cast<double>(worldX) / worldTileSize);
        const double tileZ = std::floor(static_cast<double>(worldZ) / worldTileSize);
        constexpr double minCoord = static_cast<double>(std::numeric_limits<int32_t>::min());
        constexpr double maxCoord = static_cast<double>(std::numeric_limits<int32_t>::max());
        if (!std::isfinite(tileX) || !std::isfinite(tileZ)
            || tileX < minCoord || tileX > maxCoord || tileZ < minCoord || tileZ > maxCoord)
            return false;

        coord = {static_cast<int32_t>(tileX), static_cast<int32_t>(tileZ)};
        const double localXd = static_cast<double>(worldX) - tileX * worldTileSize;
        const double localZd = static_cast<double>(worldZ) - tileZ * worldTileSize;
        if (!std::isfinite(localXd) || !std::isfinite(localZd))
            return false;

        localX = static_cast<float>(localXd);
        localZ = static_cast<float>(localZd);
        return std::isfinite(localX) && std::isfinite(localZ);
    }
}

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
        dispatcher.unregisterCommandHandler<events::terrain::BeginCreateTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::PollCreateTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::BeginTerrainLoadCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::PollTerrainLoadCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SetTerrainColliderPropertiesCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::AddTerrainTileCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::RemoveTerrainTileCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SetTerrainStreamingEnabledCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::SetTerrainStreamingConfigCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::PrepareTerrainSaveCommand>();
        dispatcher.unregisterCommandHandler<events::holeBrush::ApplyHoleBrushCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::FinalizeTerrainStrokeCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::RestoreStrokeStateCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::RestoreSurfaceMaskRegionCommand>();
        dispatcher.unregisterCommandHandler<events::physics::AddTerrainColliderCommand>();
        dispatcher.unregisterCommandHandler<events::physics::RemoveTerrainColliderCommand>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainDataQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetActiveTerrainTileSizeQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetPendingSectorTileActionCountQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::HasTerrainComponentQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::HasTerrainTileComponentQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainTileDataQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainGeometryQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainBakeGeometryQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainHeightfieldQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainHeightAtQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainLayerWeightsAtQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainLayerWeightsBatchQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainStreamingConfigQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::IsTerrainStreamingEnabledQuery>();
        dispatcher.unregisterCommandHandler<events::physics::SetPhysicsColliderStreamConfigCommand>();
        dispatcher.unregisterQueryHandler<events::physics::GetPhysicsColliderStreamConfigQuery>();
        dispatcher.unregisterQueryHandler<events::physics::HasTerrainColliderQuery>();

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
        data.heightmapRegions = comp.heightmapRegions;
        data.terrainMaterialPath = comp.terrainMaterialRef.resolve();
        data.weightMapPath = comp.weightMapPath;
        auto gridIt = terrainGrids.find(entity.id);
        data.tileCount = (gridIt != terrainGrids.end())
            ? static_cast<uint32_t>(gridIt->second->getTileCount())
            : static_cast<uint32_t>((comp.gridMaxX - comp.gridMinX + 1) *
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
            if (comp.terrainMaterialRef.isValid())
            {
                return comp.terrainMaterialRef.resolve();
            }
        }
        return {};
    }

    void TerrainService::getTerrainGridWorldBounds(glm::vec2& outMin, glm::vec2& outMax) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::TerrainComponent>();
        for (auto entity : view)
        {
            const auto& comp = view.get<components::TerrainComponent>(entity);
            if (comp.isActive)
            {
                outMin = glm::vec2(
                    static_cast<float>(comp.gridMinX) * comp.worldTileSize,
                    static_cast<float>(comp.gridMinZ) * comp.worldTileSize);
                outMax = glm::vec2(
                    static_cast<float>(comp.gridMaxX + 1) * comp.worldTileSize,
                    static_cast<float>(comp.gridMaxZ + 1) * comp.worldTileSize);
                return;
            }
        }
        outMin = outMax = glm::vec2(0.0f);
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
            // VK-1615: the grid this stroke was snapshotting is about to be destroyed, so
            // there is nothing left to restore into -- discard rather than finalize.
            if (strokeActive && strokeEntityId == entity.id)
                discardTerrainStroke();

            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(entity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                const auto& comp = registry.get<components::TerrainComponent>(ent);
                if (comp.terrainMaterialRef.isValid())
                    resource::ResourceManager::invalidateTerrainMaterialCache(comp.terrainMaterialRef);
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
                if (comp.terrainMaterialRef.isValid())
                    resource::ResourceManager::invalidateTerrainMaterialCache(comp.terrainMaterialRef);
            }
        }

        events::terrain::TerrainDeletedNotification notification;
        events::EventDispatcher::instance().publish(notification);

        // VK-1615: discard, not finalize -- every grid is going away and UndoRedoServiceImpl
        // clears its stacks on the same SceneCleared notification, so a pushed entry would
        // be dropped anyway (or worse, outlive the grid).
        discardTerrainStroke();

        terrainGrids.clear();
        fileCaches.clear();
        worldStreamers.clear();
        pendingPhysicsTiles.clear();
        pendingSectorTileActions.clear();
        worldModeActive = false;

        vfLogInfo("TerrainService: Cleared all terrains on scene clear");
    }

    void TerrainService::syncWeightMapLayerCount(uint64_t terrainEntityId, const std::string& materialPath)
    {
        if (materialPath.empty())
        {
            return;
        }

        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            return;
        }

        // With per-tile palette, weight maps are always 4 channels.
        // Just ensure all tiles have weight maps initialized.
        terrain::TerrainGrid* grid = gridIt->second.get();
        grid->initializeWeightMaps();
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

    events::terrain::TerrainBakeGeometryResult TerrainService::getTerrainBakeGeometry()
    {
        events::terrain::TerrainBakeGeometryResult result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            auto& generator = grid->getGenerator();
            auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
                return grid->getTile(coord);
            };

            // Try to use file cache to restore evicted height/LOD data
            terrain::TerrainFileCache* cache = nullptr;
            auto cacheIt = fileCaches.find(entityId);
            if (cacheIt != fileCaches.end() && cacheIt->second)
            {
                cache = cacheIt->second.get();
            }

            auto allTiles = grid->getAllTiles();
            for (auto* tile : allTiles)
            {
                if (!tile) { continue; }

                // If height data was evicted by streaming, try to reload it
                if (!tile->hasHeightData())
                {
                    if (cache)
                    {
                        cache->ensureHeightsLoaded(*tile);
                    }

                    // If still no height data (no cache or flat terrain never saved),
                    // populate as flat at height 0
                    if (!tile->hasHeightData())
                    {
                        uint32_t vc = tile->config.getVertexCount();
                        tile->heightData.resize(static_cast<size_t>(vc) * vc, 0.0f);
                    }
                }

                if (!tile->hasLODData(0))
                {
                    generator.regenerateLOD(*tile, 0, getTile);
                }

                const auto& lod0 = tile->lodLevels[0];

                events::terrain::TerrainTileGeometryInfo tileInfo;
                tileInfo.coordX = tile->coord.x;
                tileInfo.coordZ = tile->coord.z;
                tileInfo.worldOrigin = glm::vec3(tile->worldOrigin.x, 0.0f, tile->worldOrigin.z);
                tileInfo.tileSize = tile->config.worldTileSize;
                tileInfo.firstVertexIndex = static_cast<int>(result.vertices.size() / 3);
                tileInfo.vertexCount = static_cast<int>(lod0.vertices.size());

                const glm::vec3 origin(tile->worldOrigin.x, 0.0f, tile->worldOrigin.z);
                for (const auto& vertex : lod0.vertices)
                {
                    glm::vec3 worldPos = origin + vertex.position;
                    result.vertices.push_back(worldPos.x);
                    result.vertices.push_back(worldPos.y);
                    result.vertices.push_back(worldPos.z);
                }

                tileInfo.firstTriangleIndex = static_cast<int>(result.triangles.size() / 3);
                tileInfo.triangleCount = static_cast<int>(lod0.indices.size() / 3);

                int baseVertex = tileInfo.firstVertexIndex;
                for (size_t i = 0; i < lod0.indices.size(); i += 3)
                {
                    result.triangles.push_back(baseVertex + static_cast<int>(lod0.indices[i]));
                    result.triangles.push_back(baseVertex + static_cast<int>(lod0.indices[i + 1]));
                    result.triangles.push_back(baseVertex + static_cast<int>(lod0.indices[i + 2]));
                }

                result.tileInfos.push_back(tileInfo);
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
        // VK-1605: parallel per-tile presence mask — see TerrainHeightfieldResult::tileValid.
        result.tileValid.assign(static_cast<size_t>(gridCountX) * gridCountZ, 0u);

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
            if (tileIndex < result.tileValid.size())
                result.tileValid[tileIndex] = 1u;
        }

        result.valid = true;
        return result;
    }

    terrain::TerrainHeightAtResult TerrainService::getTerrainHeightAt(float worldX, float worldZ)
    {
        terrain::TerrainHeightAtResult result;

        if (terrainGrids.empty())
            return result;

        auto& [entityId, grid] = *terrainGrids.begin();
        auto allTiles = grid->getAllTiles();
        if (allTiles.empty())
            return result;

        const auto& config = allTiles[0]->config;
        float tileSize = config.worldTileSize;
        uint32_t quadCount = config.getQuadCount();
        float vertexSpacing = config.getVertexSpacing();

        // Determine which tile this world position falls in
        int32_t tileX = static_cast<int32_t>(std::floor(worldX / tileSize));
        int32_t tileZ = static_cast<int32_t>(std::floor(worldZ / tileSize));

        // Find the tile
        terrain::TerrainTile* tile = nullptr;
        for (auto* t : allTiles)
        {
            if (t && t->coord.x == tileX && t->coord.z == tileZ)
            {
                tile = t;
                break;
            }
        }

        if (!tile || !tile->hasHeightData())
            return result;

        // Local position within the tile (0..tileSize)
        float localX = worldX - static_cast<float>(tileX) * tileSize;
        float localZ = worldZ - static_cast<float>(tileZ) * tileSize;

        // Convert to grid coordinates (fractional)
        float gx = localX / vertexSpacing;
        float gz = localZ / vertexSpacing;

        // Clamp to valid range
        float maxCoord = static_cast<float>(quadCount);
        gx = std::clamp(gx, 0.0f, maxCoord);
        gz = std::clamp(gz, 0.0f, maxCoord);

        // Integer grid indices
        uint32_t ix = static_cast<uint32_t>(gx);
        uint32_t iz = static_cast<uint32_t>(gz);
        ix = std::min(ix, quadCount - 1);
        iz = std::min(iz, quadCount - 1);

        // Fractional part for bilinear interpolation
        float fx = gx - static_cast<float>(ix);
        float fz = gz - static_cast<float>(iz);

        uint32_t vpt = quadCount + 1;
        auto getHeight = [&](uint32_t x, uint32_t z) -> float
        {
            return tile->heightData[z * vpt + x];
        };

        // Bilinear interpolation of the four surrounding vertices
        float h00 = getHeight(ix, iz);
        float h10 = getHeight(ix + 1, iz);
        float h01 = getHeight(ix, iz + 1);
        float h11 = getHeight(ix + 1, iz + 1);

        float h = h00 * (1.0f - fx) * (1.0f - fz)
                + h10 * fx * (1.0f - fz)
                + h01 * (1.0f - fx) * fz
                + h11 * fx * fz;

        result.height = h;
        result.valid = true;
        return result;
    }

    terrain::TerrainLayerWeightsAtResult TerrainService::getTerrainLayerWeightsAt(float worldX,
                                                                                    float worldZ)
    {
        terrain::TerrainLayerWeightsAtResult result;
        if (terrainGrids.empty() || !terrainGrids.begin()->second)
            return result;

        // Terrain queries currently follow the engine's single-terrain-per-scene convention.
        auto& grid = *terrainGrids.begin()->second;
        const auto& config = grid.getTileConfig();
        const float vertexSpacing = config.getVertexSpacing();
        if (!std::isfinite(vertexSpacing) || vertexSpacing <= 0.0f)
            return result;

        terrain::TileCoord coord;
        float localX = 0.0f;
        float localZ = 0.0f;
        if (!resolveTerrainSamplePosition(worldX, worldZ, config.worldTileSize,
                                          coord, localX, localZ))
            return result;

        const terrain::TerrainTile* tile = grid.getTile(coord);
        if (!tile || !tile->hasWeightMap())
            return result;

        terrain::sampleTileLayerWeightsBilinear(tile->weightMap, localX, localZ,
                                                vertexSpacing, result);
        terrain::compactLayerWeights(result, terrain::TERRAIN_LAYER_WEIGHT_EPSILON);
        return result;
    }

    std::vector<terrain::TerrainLayerWeightsAtResult> TerrainService::getTerrainLayerWeightsBatch(
        const std::vector<glm::vec2>& positions)
    {
        std::vector<terrain::TerrainLayerWeightsAtResult> results(positions.size());
        if (positions.empty() || terrainGrids.empty() || !terrainGrids.begin()->second)
            return results;

        const auto& [entityId, gridPtr] = *terrainGrids.begin();
        auto& grid = *gridPtr;
        const auto& config = grid.getTileConfig();
        const float vertexSpacing = config.getVertexSpacing();
        if (!std::isfinite(vertexSpacing) || vertexSpacing <= 0.0f)
            return results;

        const auto cacheIt = fileCaches.find(entityId);
        const std::shared_ptr<terrain::TerrainFileCache> fileCache =
            cacheIt != fileCaches.end() ? cacheIt->second : nullptr;

        bool memoInitialized = false;
        terrain::TileCoord memoCoord{};
        terrain::TerrainTile* memoTile = nullptr;

        for (size_t i = 0; i < positions.size(); ++i)
        {
            terrain::TileCoord coord;
            float localX = 0.0f;
            float localZ = 0.0f;
            if (!resolveTerrainSamplePosition(positions[i].x, positions[i].y,
                                              config.worldTileSize, coord, localX, localZ))
                continue;

            if (!memoInitialized || coord.x != memoCoord.x || coord.z != memoCoord.z)
            {
                memoInitialized = true;
                memoCoord = coord;
                memoTile = grid.getTile(coord);
                if (memoTile && !memoTile->hasWeightMap() && !memoTile->hasHeightData() && fileCache)
                    fileCache->ensureHeightsLoaded(*memoTile);
            }

            if (!memoTile || !memoTile->hasWeightMap())
                continue;

            terrain::sampleTileLayerWeightsBilinear(memoTile->weightMap, localX, localZ,
                                                    vertexSpacing, results[i]);
            terrain::compactLayerWeights(results[i], terrain::TERRAIN_LAYER_WEIGHT_EPSILON);
        }
        return results;
    }
}
