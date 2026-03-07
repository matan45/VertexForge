#include "WaterService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "water/WaterGrid.hpp"
#include "water/WaterTile.hpp"
#include "water/WaterTypes.hpp"
#include "water/WaterSerializer.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/WaterEvents.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"
#include <filesystem>

namespace services
{
    EntityHandle WaterService::createWater(const WaterCreationData& config)
    {
        water::WaterTileConfig tileConfig;
        tileConfig.worldTileSize = config.worldTileSize;

        int32_t halfX = config.tilesX / 2;
        int32_t halfZ = config.tilesZ / 2;
        int32_t minX = -halfX;
        int32_t minZ = -halfZ;
        int32_t maxX = config.tilesX - halfX - 1;
        int32_t maxZ = config.tilesZ - halfZ - 1;

        auto grid = std::make_unique<water::WaterGrid>(tileConfig, config.waterHeight);
        grid->createGrid(minX, minZ, maxX, maxZ);

        scene::Entity parentEntity("Water");
        sceneGraph->addChild(sceneGraph->GetRoot(), parentEntity);

        auto& waterComp = parentEntity.addComponent<components::WaterComponent>();
        waterComp.worldTileSize = config.worldTileSize;
        waterComp.globalDensity = 1000.0f;
        waterComp.globalDrag = 0.5f;
        waterComp.globalBuoyancyStrength = 2.0f;
        waterComp.defaultWaterHeight = config.waterHeight;
        waterComp.defaultWaveIntensity = config.waveIntensity;
        waterComp.shallowColor = config.shallowColor;
        waterComp.deepColor = config.deepColor;
        int32_t boundsMinX, boundsMinZ, boundsMaxX, boundsMaxZ;
        grid->computeBounds(boundsMinX, boundsMinZ, boundsMaxX, boundsMaxZ);
        waterComp.gridMinX = boundsMinX;
        waterComp.gridMinZ = boundsMinZ;
        waterComp.gridMaxX = boundsMaxX;
        waterComp.gridMaxZ = boundsMaxZ;
        waterComp.physicsEnabled = config.physicsEnabled;
        waterComp.isActive = true;
        waterComp.activeTileCount = static_cast<uint32_t>(grid->getTileCount());
        waterComp.visibleTileCount = 0;

        EntityHandle parentHandle = internal::toHandle(parentEntity.getHandle());

        createTileEntities(parentHandle, *grid);

        waterGrids[parentHandle.id] = std::move(grid);
        populateDefinitionMap(parentHandle.id, *waterGrids[parentHandle.id]);
        waterStreamers[parentHandle.id] = std::make_unique<water::WaterWorldStreamer>();

        events::water::WaterCreatedNotification notification;
        notification.waterEntity = parentHandle;
        notification.config = config;
        events::EventDispatcher::instance().publish(notification);

        globalSettingsDirty = true;

        vfLogInfo("Created water with {} tiles", config.tilesX * config.tilesZ);

        return parentHandle;
    }

    void WaterService::createTileEntity(EntityHandle parentHandle, water::WaterTile* tile, float tileSize)
    {
        scene::Entity parentEntity(internal::fromHandle(parentHandle));

        std::string tileName = "WaterTile_" + std::to_string(tile->coord.x) +
                               "_" + std::to_string(tile->coord.z);
        scene::Entity tileEntity(tileName);
        parentEntity.addChildren(tileEntity);

        auto& tileComp = tileEntity.addComponent<components::WaterTileComponent>();
        tileComp.tileX = tile->coord.x;
        tileComp.tileZ = tile->coord.z;
        tileComp.waterHeight = tile->waterHeight;
        tileComp.waveIntensity = tile->waveIntensity;
        tileComp.physicsEnabled = tile->physicsEnabled;
        tileComp.isVisible = tile->isVisible;

        auto& transform = tileEntity.getComponent<components::TransformComponent>();
        transform.position = tile->worldOrigin;
        transform.isDirty = true;

        if (physicsProvider && tileComp.physicsEnabled)
        {
            EntityHandle tileHandle = internal::toHandle(tileEntity.getHandle());
            glm::vec3 halfExtents(tileSize * 0.5f, 0.5f, tileSize * 0.5f);
            glm::vec3 position = tile->worldOrigin + halfExtents;
            physicsProvider->addWaterSensorBody(tileHandle, position, halfExtents);
        }
    }

    void WaterService::createTileEntities(EntityHandle parentHandle, water::WaterGrid& grid)
    {
        float tileSize = grid.getConfig().worldTileSize;
        for (auto* tile : grid.getAllTiles())
        {
            createTileEntity(parentHandle, tile, tileSize);
        }
    }

    bool WaterService::deleteWater(EntityHandle waterEntity)
    {
        if (!waterEntity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity entity = internal::fromHandle(waterEntity);

        if (!registry.valid(entity))
            return false;

        if (!registry.all_of<components::WaterComponent>(entity))
            return false;

        waterGrids.erase(waterEntity.id);
        definitionMaps.erase(waterEntity.id);
        waterStreamers.erase(waterEntity.id);
        globalSettingsDirty = true;

        scene::Entity waterEnt(entity);
        sceneGraph->removeEntity(waterEnt);

        events::water::WaterDeletedNotification notification;
        notification.waterEntity = waterEntity;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    void WaterService::onEntityDeleted(EntityHandle entity)
    {
        if (!entity.isValid())
            return;

        if (physicsProvider)
        {
            physicsProvider->removeWaterSensorBody(entity);
        }

        auto it = waterGrids.find(entity.id);
        if (it != waterGrids.end())
        {
            waterGrids.erase(it);
            definitionMaps.erase(entity.id);
            waterStreamers.erase(entity.id);
            globalSettingsDirty = true;

            events::water::WaterDeletedNotification notification;
            notification.waterEntity = entity;
            events::EventDispatcher::instance().publish(notification);
        }
    }

    void WaterService::onSceneCleared()
    {
        entitiesInWater.clear();
        globalSettingsDirty = true;

        oceanFFTEnabled = false;
        oceanConfig = OceanFFTConfigData{};
        oceanConfigVersion++;

        if (waterGrids.empty())
            return;

        waterGrids.clear();
        definitionMaps.clear();
        waterStreamers.clear();
        vfLogInfo("WaterService: Cleared all water on scene clear");
    }

    void WaterService::rebuildWaterFromComponents()
    {
        waterGrids.clear();
        definitionMaps.clear();
        waterStreamers.clear();
        entitiesInWater.clear();
        globalSettingsDirty = true;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto waterView = registry.view<components::WaterComponent>();

        for (auto entity : waterView)
        {
            auto& waterComp = registry.get<components::WaterComponent>(entity);
            EntityHandle waterHandle = internal::toHandle(entity);

            // If a .vfWater file reference exists, load from it
            if (!waterComp.savePath.empty() && std::filesystem::exists(waterComp.savePath))
            {
                water::WaterLoadResult loadResult;
                if (water::WaterSerializer::loadAll(waterComp.savePath, loadResult))
                {
                    const auto& header = loadResult.header;

                    // Apply loaded settings to component
                    const auto& s = header.globalSettings;
                    waterComp.globalDensity = s.density;
                    waterComp.globalDrag = s.drag;
                    waterComp.globalBuoyancyStrength = s.buoyancyStrength;
                    waterComp.waveSpeed = s.waveSpeed;
                    waterComp.waveAmplitude = s.waveAmplitude;
                    waterComp.waveFrequency = s.waveFrequency;
                    waterComp.shallowColor = s.shallowColor;
                    waterComp.deepColor = s.deepColor;
                    waterComp.maxVisibleDepth = s.maxVisibleDepth;
                    waterComp.fresnelPower = s.fresnelPower;
                    waterComp.dudvTiling = s.dudvTiling;
                    waterComp.dudvStrength = s.dudvStrength;
                    waterComp.waveDirectionDegrees = s.waveDirectionDegrees;
                    waterComp.physicsEnabled = header.physicsEnabled;

                    water::WaterTileConfig tileConfig;
                    tileConfig.worldTileSize = header.worldTileSize;

                    auto grid = std::make_unique<water::WaterGrid>(tileConfig, 0.0f);

                    // Remove existing tile children (scene JSON may have stale ones)
                    scene::Entity waterEntity(entity);
                    auto children = waterEntity.getChildren();
                    for (auto& child : children)
                    {
                        if (child.hasComponent<components::WaterTileComponent>())
                            sceneGraph->removeEntity(child);
                    }

                    // Create only tiles that exist in the file (sparse)
                    for (const auto& tileData : loadResult.tiles)
                    {
                        water::WaterTile* tile = grid->getOrCreateTile(
                            water::TileCoord(tileData.tileX, tileData.tileZ));
                        if (tile)
                        {
                            tile->updateHeight(tileData.waterHeight, tileConfig.worldTileSize);
                            tile->waveIntensity = tileData.waveIntensity;
                            tile->physicsEnabled = tileData.physicsEnabled;
                        }
                    }

                    createTileEntities(waterHandle, *grid);

                    if (!loadResult.tiles.empty())
                    {
                        waterComp.defaultWaterHeight = loadResult.tiles[0].waterHeight;
                        waterComp.defaultWaveIntensity = loadResult.tiles[0].waveIntensity;
                    }

                    int32_t rbMinX, rbMinZ, rbMaxX, rbMaxZ;
                    grid->computeBounds(rbMinX, rbMinZ, rbMaxX, rbMaxZ);
                    waterComp.gridMinX = rbMinX;
                    waterComp.gridMinZ = rbMinZ;
                    waterComp.gridMaxX = rbMaxX;
                    waterComp.gridMaxZ = rbMaxZ;
                    waterComp.activeTileCount = static_cast<uint32_t>(grid->getTileCount());

                    waterGrids[waterHandle.id] = std::move(grid);
                    populateDefinitionMap(waterHandle.id, *waterGrids[waterHandle.id]);
                    waterStreamers[waterHandle.id] = std::make_unique<water::WaterWorldStreamer>();

                    // Restore ocean FFT config from .vfWater file
                    const auto& o = header.oceanSettings;
                    oceanConfig.resolution = o.resolution;
                    oceanConfig.patchSize = o.patchSize;
                    oceanConfig.windSpeed = o.windSpeed;
                    oceanConfig.windDirection = o.windDirection;
                    oceanConfig.amplitude = o.amplitude;
                    oceanConfig.choppiness = o.choppiness;
                    oceanConfig.gravity = o.gravity;
                    oceanConfig.foamThreshold = o.foamThreshold;
                    oceanConfig.enabled = o.enabled;
                    oceanFFTEnabled = o.enabled;
                    oceanConfigVersion++;

                    vfLogInfo("WaterService: Rebuilt water from .vfWater file: {}", waterComp.savePath);
                    continue;
                }
                else
                {
                    vfLogWarning("WaterService: Failed to load .vfWater file '{}', falling back to component data",
                                 waterComp.savePath);
                }
            }

            // Fallback: rebuild from inline component/tile data (sparse)
            water::WaterTileConfig tileConfig;
            tileConfig.worldTileSize = waterComp.worldTileSize;

            auto grid = std::make_unique<water::WaterGrid>(tileConfig, waterComp.defaultWaterHeight);

            scene::Entity waterEntity(entity);
            for (auto& child : waterEntity.getChildren())
            {
                if (!child.hasComponent<components::WaterTileComponent>())
                    continue;

                const auto& tileComp = child.getComponent<components::WaterTileComponent>();
                water::WaterTile* tile = grid->getOrCreateTile(water::TileCoord(tileComp.tileX, tileComp.tileZ));
                if (tile)
                {
                    tile->updateHeight(tileComp.waterHeight, tileConfig.worldTileSize);
                    tile->waveIntensity = tileComp.waveIntensity;
                    tile->physicsEnabled = tileComp.physicsEnabled;
                    tile->isVisible = tileComp.isVisible;
                }

                if (physicsProvider && tileComp.physicsEnabled)
                {
                    EntityHandle tileHandle = internal::toHandle(child.getHandle());
                    glm::vec3 halfExtents(tileConfig.worldTileSize * 0.5f, 0.5f,
                                          tileConfig.worldTileSize * 0.5f);
                    auto& transform = child.getComponent<components::TransformComponent>();
                    glm::vec3 position = transform.position + halfExtents;
                    physicsProvider->addWaterSensorBody(tileHandle, position, halfExtents);
                }
            }

            // Recompute bounds from actual tiles
            int32_t fbMinX, fbMinZ, fbMaxX, fbMaxZ;
            grid->computeBounds(fbMinX, fbMinZ, fbMaxX, fbMaxZ);
            waterComp.gridMinX = fbMinX;
            waterComp.gridMinZ = fbMinZ;
            waterComp.gridMaxX = fbMaxX;
            waterComp.gridMaxZ = fbMaxZ;
            waterComp.activeTileCount = static_cast<uint32_t>(grid->getTileCount());

            waterGrids[waterHandle.id] = std::move(grid);
            populateDefinitionMap(waterHandle.id, *waterGrids[waterHandle.id]);
            waterStreamers[waterHandle.id] = std::make_unique<water::WaterWorldStreamer>();

            // Restore ocean FFT config from inline component data
            oceanConfig.resolution = waterComp.oceanResolution;
            oceanConfig.patchSize = waterComp.oceanPatchSize;
            oceanConfig.windSpeed = waterComp.oceanWindSpeed;
            oceanConfig.windDirection = waterComp.oceanWindDirection;
            oceanConfig.amplitude = waterComp.oceanAmplitude;
            oceanConfig.choppiness = waterComp.oceanChoppiness;
            oceanConfig.gravity = waterComp.oceanGravity;
            oceanConfig.foamThreshold = waterComp.oceanFoamThreshold;
            oceanConfig.enabled = waterComp.oceanFFTEnabled;
            oceanFFTEnabled = waterComp.oceanFFTEnabled;
            oceanConfigVersion++;
        }

        if (!waterGrids.empty())
        {
            vfLogInfo("WaterService: Rebuilt {} water grid(s) from components", waterGrids.size());
        }
    }

    bool WaterService::addTile(EntityHandle waterEntity, int32_t tileX, int32_t tileZ)
    {
        if (!waterEntity.isValid())
            return false;

        auto gridIt = waterGrids.find(waterEntity.id);
        if (gridIt == waterGrids.end())
            return false;

        auto& grid = *gridIt->second;
        water::TileCoord coord(tileX, tileZ);

        if (grid.hasTile(coord))
            return false;

        water::WaterTile* tile = grid.getOrCreateTile(coord);
        if (!tile)
            return false;

        float tileSize = grid.getConfig().worldTileSize;
        createTileEntity(waterEntity, tile, tileSize);

        // Keep definition map in sync
        water::WaterTileDefinition def;
        def.waterHeight = tile->waterHeight;
        def.waveIntensity = tile->waveIntensity;
        def.physicsEnabled = tile->physicsEnabled;
        definitionMaps[waterEntity.id].addDefinition(coord, def);

        // Update component bounds
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(waterEntity);
        if (registry.valid(ent) && registry.all_of<components::WaterComponent>(ent))
        {
            auto& comp = registry.get<components::WaterComponent>(ent);
            int32_t minX, minZ, maxX, maxZ;
            grid.computeBounds(minX, minZ, maxX, maxZ);
            comp.gridMinX = minX;
            comp.gridMinZ = minZ;
            comp.gridMaxX = maxX;
            comp.gridMaxZ = maxZ;
            comp.activeTileCount = static_cast<uint32_t>(grid.getTileCount());
        }

        events::water::WaterTileAddedNotification notification;
        notification.waterEntity = waterEntity;
        notification.tileX = tileX;
        notification.tileZ = tileZ;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    bool WaterService::removeTile(EntityHandle waterEntity, int32_t tileX, int32_t tileZ)
    {
        if (!waterEntity.isValid())
            return false;

        auto gridIt = waterGrids.find(waterEntity.id);
        if (gridIt == waterGrids.end())
            return false;

        auto& grid = *gridIt->second;
        water::TileCoord coord(tileX, tileZ);

        if (!grid.hasTile(coord))
            return false;

        // Find and destroy the child entity with matching tile coordinates
        scene::Entity parentEntity(internal::fromHandle(waterEntity));
        for (auto& child : parentEntity.getChildren())
        {
            if (!child.hasComponent<components::WaterTileComponent>())
                continue;

            const auto& tileComp = child.getComponent<components::WaterTileComponent>();
            if (tileComp.tileX == tileX && tileComp.tileZ == tileZ)
            {
                EntityHandle tileHandle = internal::toHandle(child.getHandle());
                if (physicsProvider)
                {
                    physicsProvider->removeWaterSensorBody(tileHandle);
                }
                sceneGraph->removeEntity(child);
                break;
            }
        }

        grid.removeTile(coord);
        definitionMaps[waterEntity.id].removeDefinition(coord);

        // Update component bounds
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(waterEntity);
        if (registry.valid(ent) && registry.all_of<components::WaterComponent>(ent))
        {
            auto& comp = registry.get<components::WaterComponent>(ent);
            int32_t minX, minZ, maxX, maxZ;
            grid.computeBounds(minX, minZ, maxX, maxZ);
            comp.gridMinX = minX;
            comp.gridMinZ = minZ;
            comp.gridMaxX = maxX;
            comp.gridMaxZ = maxZ;
            comp.activeTileCount = static_cast<uint32_t>(grid.getTileCount());
        }

        events::water::WaterTileRemovedNotification notification;
        notification.waterEntity = waterEntity;
        notification.tileX = tileX;
        notification.tileZ = tileZ;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    void WaterService::remapWaterEntities()
    {
        if (waterGrids.empty())
            return;

        std::vector<std::unique_ptr<water::WaterGrid>> grids;
        std::vector<water::WaterDefinitionMap> defMaps;
        std::vector<std::unique_ptr<water::WaterWorldStreamer>> streamers;
        for (auto& [id, grid] : waterGrids)
        {
            grids.push_back(std::move(grid));
            auto defIt = definitionMaps.find(id);
            defMaps.push_back(defIt != definitionMaps.end() ? std::move(defIt->second) : water::WaterDefinitionMap{});
            auto streamerIt = waterStreamers.find(id);
            streamers.push_back(streamerIt != waterStreamers.end() ? std::move(streamerIt->second) : nullptr);
        }
        waterGrids.clear();
        definitionMaps.clear();
        waterStreamers.clear();
        entitiesInWater.clear();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WaterComponent>();

        size_t gridIndex = 0;
        for (auto entity : view)
        {
            if (gridIndex >= grids.size())
                break;

            uint64_t newId = internal::toHandle(entity).id;
            waterGrids[newId] = std::move(grids[gridIndex]);
            definitionMaps[newId] = std::move(defMaps[gridIndex]);
            if (streamers[gridIndex])
                waterStreamers[newId] = std::move(streamers[gridIndex]);

            if (physicsProvider)
            {
                scene::Entity waterEntity(entity);
                const auto& waterComp = registry.get<components::WaterComponent>(entity);
                float tileSize = waterComp.worldTileSize;

                for (auto& child : waterEntity.getChildren())
                {
                    if (!child.hasComponent<components::WaterTileComponent>())
                        continue;

                    const auto& tileComp = child.getComponent<components::WaterTileComponent>();
                    if (tileComp.physicsEnabled)
                    {
                        EntityHandle tileHandle = internal::toHandle(child.getHandle());
                        glm::vec3 halfExtents(tileSize * 0.5f, 0.5f, tileSize * 0.5f);
                        auto& transform = child.getComponent<components::TransformComponent>();
                        glm::vec3 position = transform.position + halfExtents;
                        physicsProvider->addWaterSensorBody(tileHandle, position, halfExtents);
                    }
                }
            }

            gridIndex++;
        }

        vfLogInfo("WaterService: Remapped {} water grid(s) to new entity IDs", waterGrids.size());
    }
}
