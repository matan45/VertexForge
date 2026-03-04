#include "WaterService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "water/WaterGrid.hpp"
#include "water/WaterTile.hpp"
#include "water/WaterTypes.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/WaterEvents.hpp"
#include "../../providers/IPhysicsProvider.hpp"

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
        waterComp.gridMinX = minX;
        waterComp.gridMinZ = minZ;
        waterComp.gridMaxX = maxX;
        waterComp.gridMaxZ = maxZ;
        waterComp.physicsEnabled = config.physicsEnabled;
        waterComp.isActive = true;
        waterComp.activeTileCount = static_cast<uint32_t>(config.tilesX * config.tilesZ);
        waterComp.visibleTileCount = 0;

        EntityHandle parentHandle = internal::toHandle(parentEntity.getHandle());

        createTileEntities(parentHandle, *grid);

        waterGrids[parentHandle.id] = std::move(grid);

        events::water::WaterCreatedNotification notification;
        notification.waterEntity = parentHandle;
        notification.config = config;
        events::EventDispatcher::instance().publish(notification);

        globalSettingsDirty = true;

        vfLogInfo("Created water with {} tiles", config.tilesX * config.tilesZ);

        return parentHandle;
    }

    void WaterService::createTileEntities(EntityHandle parentHandle, water::WaterGrid& grid)
    {
        scene::Entity parentEntity(internal::fromHandle(parentHandle));
        float tileSize = grid.getConfig().worldTileSize;

        for (auto* tile : grid.getAllTiles())
        {
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

        if (waterGrids.empty())
            return;

        waterGrids.clear();
        vfLogInfo("WaterService: Cleared all water on scene clear");
    }

    void WaterService::rebuildWaterFromComponents()
    {
        waterGrids.clear();
        entitiesInWater.clear();
        globalSettingsDirty = true;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto waterView = registry.view<components::WaterComponent>();

        for (auto entity : waterView)
        {
            const auto& waterComp = registry.get<components::WaterComponent>(entity);
            EntityHandle waterHandle = internal::toHandle(entity);

            water::WaterTileConfig tileConfig;
            tileConfig.worldTileSize = waterComp.worldTileSize;

            auto grid = std::make_unique<water::WaterGrid>(tileConfig, waterComp.defaultWaterHeight);
            grid->createGrid(waterComp.gridMinX, waterComp.gridMinZ,
                             waterComp.gridMaxX, waterComp.gridMaxZ);

            scene::Entity waterEntity(entity);
            for (auto& child : waterEntity.getChildren())
            {
                if (!child.hasComponent<components::WaterTileComponent>())
                    continue;

                const auto& tileComp = child.getComponent<components::WaterTileComponent>();
                water::WaterTile* tile = grid->getTile(water::TileCoord(tileComp.tileX, tileComp.tileZ));
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

            waterGrids[waterHandle.id] = std::move(grid);
        }

        if (!waterGrids.empty())
        {
            vfLogInfo("WaterService: Rebuilt {} water grid(s) from components", waterGrids.size());
        }
    }

    void WaterService::remapWaterEntities()
    {
        if (waterGrids.empty())
            return;

        std::vector<std::unique_ptr<water::WaterGrid>> grids;
        for (auto& [id, grid] : waterGrids)
        {
            grids.push_back(std::move(grid));
        }
        waterGrids.clear();
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
