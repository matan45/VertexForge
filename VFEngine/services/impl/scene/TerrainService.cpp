#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/HeightmapLoader.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/TerrainEvents.hpp"
#include "print/EditorLogger.hpp"

namespace services
{
    TerrainService::TerrainService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
    {
    }

    TerrainService::~TerrainService()
    {
        // Unregister event handlers to prevent dangling references
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<events::terrain::CreateTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::DeleteTerrainCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::UploadTerrainTilesCommand>();
        dispatcher.unregisterCommandHandler<events::terrain::UpdateTerrainLODsCommand>();
        dispatcher.unregisterQueryHandler<events::terrain::GetTerrainDataQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::HasTerrainComponentQuery>();
        dispatcher.unregisterQueryHandler<events::terrain::GetVisibleTerrainTilesQuery>();

        terrainGrids.clear();
    }

    void TerrainService::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::terrain::CreateTerrainCommand>(
            [this](const events::terrain::CreateTerrainCommand& cmd)
            {
                return createTerrain(cmd.config);
            });

        dispatcher.registerCommandHandler<events::terrain::DeleteTerrainCommand>(
            [this](const events::terrain::DeleteTerrainCommand& cmd)
            {
                return deleteTerrain(cmd.terrainEntity);
            });

        dispatcher.registerQueryHandler<events::terrain::GetTerrainDataQuery>(
            [this](const events::terrain::GetTerrainDataQuery& query)
            {
                return getTerrainData(query.entity);
            });

        dispatcher.registerQueryHandler<events::terrain::HasTerrainComponentQuery>(
            [this](const events::terrain::HasTerrainComponentQuery& query)
            {
                return hasTerrainComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::terrain::GetVisibleTerrainTilesQuery>(
            [this](const events::terrain::GetVisibleTerrainTilesQuery& query)
            {
                return collectVisibleTiles(query.frustum, query.cameraPosition);
            });

        dispatcher.registerCommandHandler<events::terrain::UpdateTerrainLODsCommand>(
            [this](const events::terrain::UpdateTerrainLODsCommand& cmd)
            {
                updateAllTerrainLODs(cmd.cameraPosition);
            });

        dispatcher.registerCommandHandler<events::terrain::UploadTerrainTilesCommand>(
            [this](const events::terrain::UploadTerrainTilesCommand& cmd)
            {
                // Terrain GPU upload is handled by the adapter in graphics layer
                // This command exists for future use if needed
                return true;
            });
    }

    EntityHandle TerrainService::createTerrain(const TerrainCreationData& config)
    {
        // 1. Create terrain configuration
        terrain::TerrainTileConfig tileConfig;

        // Map resolution index to TileResolution enum
        switch (config.resolution)
        {
        case 0: tileConfig.resolution = terrain::TileResolution::Low; break;
        case 1: tileConfig.resolution = terrain::TileResolution::Medium; break;
        case 2: tileConfig.resolution = terrain::TileResolution::High; break;
        case 3: tileConfig.resolution = terrain::TileResolution::Ultra; break;
        default: tileConfig.resolution = terrain::TileResolution::Low; break;
        }

        tileConfig.worldTileSize = config.worldTileSize;
        tileConfig.maxHeight = config.maxHeight;
        tileConfig.minHeight = config.minHeight;

        for (int i = 0; i < 4; ++i)
        {
            tileConfig.lodDistances[i] = config.lodDistances[i];
        }

        // 2. Calculate grid bounds (centered around origin)
        int32_t halfX = config.tilesX / 2;
        int32_t halfZ = config.tilesZ / 2;
        int32_t minX = -halfX;
        int32_t minZ = -halfZ;
        int32_t maxX = config.tilesX - halfX - 1;
        int32_t maxZ = config.tilesZ - halfZ - 1;

        // 3. Create the terrain grid
        auto grid = std::make_unique<terrain::TerrainGrid>(tileConfig);

        // Set height sampler (flat terrain if no heightmap, otherwise load heightmap)
        if (config.heightmapPath.empty())
        {
            // Flat terrain - height sampler returns 0
            grid->setHeightSampler([](float /*worldX*/, float /*worldZ*/) -> float {
                return 0.0f;
            });
        }
        else
        {
            // Load heightmap and create sampler
            auto heightmapData = terrain::HeightmapLoader::load(config.heightmapPath);
            if (heightmapData && heightmapData->isValid())
            {
                // Calculate terrain world bounds
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

        // 4. Create the grid tiles
        grid->createGrid(minX, minZ, maxX, maxZ, nullptr);

        // 5. Create parent entity
        scene::Entity parentEntity("Terrain");
        sceneGraph->addChild(sceneGraph->GetRoot(), parentEntity);

        // 6. Add TerrainComponent to parent
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

        EntityHandle parentHandle = internal::toHandle(parentEntity.getHandle());

        // 7. Create tile child entities
        createTileEntities(parentHandle, *grid);

        // 8. Store the grid (owned by this service)
        terrainGrids[parentHandle.id] = std::move(grid);

        // 9. Publish notification
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
            // Create entity name based on tile coordinate
            std::string tileName = "Tile_" + std::to_string(tile->coord.x) + "_" + std::to_string(tile->coord.z);
            scene::Entity tileEntity(tileName);

            // Add as child of parent
            parentEntity.addChildren(tileEntity);

            // Add TerrainTileComponent
            auto& tileComp = tileEntity.addComponent<components::TerrainTileComponent>();
            tileComp.tileX = tile->coord.x;
            tileComp.tileZ = tile->coord.z;
            tileComp.currentLOD = tile->currentLOD;
            tileComp.isVisible = tile->isVisible;

            // Set tile entity position based on world origin
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

        // Check if it has terrain component
        if (!registry.all_of<components::TerrainComponent>(entity))
            return false;

        // Remove grid from our storage
        terrainGrids.erase(terrainEntity.id);

        // Use scene graph's proper deletion (handles recursive child removal and parent cleanup)
        scene::Entity terrainEnt(entity);
        sceneGraph->removeEntity(terrainEnt);

        // Publish notification so graphics layer can clear GPU buffers
        events::terrain::TerrainDeletedNotification notification;
        notification.terrainEntity = terrainEntity;
        events::EventDispatcher::instance().publish(notification);

        return true;
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
        data.tileCount = static_cast<uint32_t>((comp.gridMaxX - comp.gridMinX + 1) *
                                                (comp.gridMaxZ - comp.gridMinZ + 1));

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

    std::vector<events::terrain::TerrainTileInfo> TerrainService::collectVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<events::terrain::TerrainTileInfo> result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            // First update LODs based on camera position
            grid->updateLODs(cameraPosition);

            // Get visible tiles
            auto visibleTiles = grid->getVisibleTiles(frustum);

            // Convert visible tiles to TerrainTileInfo
            for (terrain::TerrainTile* tile : visibleTiles)
            {
                if (!tile || !tile->isVisible)
                    continue;

                // Check if tile has geometry data for current LOD
                const auto& lodData = tile->getCurrentLODData();
                if (lodData.isEmpty() || !lodData.hasMeshlets())
                    continue;

                events::terrain::TerrainTileInfo info;
                info.coordX = tile->coord.x;
                info.coordZ = tile->coord.z;
                info.currentLOD = tile->currentLOD;
                info.worldOrigin = tile->worldOrigin;
                info.aabbMin = tile->worldBounds.min;
                info.aabbMax = tile->worldBounds.max;

                result.push_back(info);
            }
        }

        return result;
    }

    void TerrainService::updateAllTerrainLODs(const glm::vec3& cameraPosition)
    {
        for (auto& [entityId, grid] : terrainGrids)
        {
            grid->updateLODs(cameraPosition);
        }
    }

    std::vector<terrain::TerrainTile*> TerrainService::getRawVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<terrain::TerrainTile*> result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            // Update LODs first
            grid->updateLODs(cameraPosition);

            // Get visible tiles from this grid
            auto visibleTiles = grid->getVisibleTiles(frustum);

            // Filter to only tiles with valid geometry
            for (terrain::TerrainTile* tile : visibleTiles)
            {
                if (tile && tile->isVisible)
                {
                    const auto& lodData = tile->getCurrentLODData();
                    if (!lodData.isEmpty() && lodData.hasMeshlets())
                    {
                        result.push_back(tile);
                    }
                }
            }
        }

        return result;
    }

    size_t TerrainService::getTotalTileCount() const
    {
        size_t count = 0;
        for (const auto& [entityId, grid] : terrainGrids)
        {
            count += grid->getAllTiles().size();
        }
        return count;
    }
}
