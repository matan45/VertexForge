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
#include "../../events/terrain/WaterEvents.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"

namespace services
{
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

    void WaterService::loadAllWaterTiles(EntityHandle waterEntity)
    {
        uint64_t entityId = waterEntity.id;

        auto gridIt = waterGrids.find(entityId);
        if (gridIt == waterGrids.end())
            return;

        auto defIt = definitionMaps.find(entityId);
        if (defIt == definitionMaps.end())
            return;

        auto& grid = *gridIt->second;
        float tileSize = grid.getConfig().worldTileSize;

        // Disable streaming so tiles won't be unloaded again
        auto streamerIt = waterStreamers.find(entityId);
        if (streamerIt != waterStreamers.end() && streamerIt->second)
        {
            streamerIt->second->setEnabled(false);
        }

        // Load all defined tiles that aren't already in the grid
        defIt->second.forEachDefinition([&](const water::TileCoord& coord,
                                             const water::WaterTileDefinition& def)
        {
            if (grid.hasTile(coord))
                return;

            water::WaterTile* tile = grid.getOrCreateTile(coord);
            if (tile)
            {
                tile->updateHeight(def.waterHeight, tileSize);
                tile->waveIntensity = def.waveIntensity;
                tile->physicsEnabled = def.physicsEnabled;
                createTileEntity(waterEntity, tile, tileSize);
            }
        });

        // Sync component bounds
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
    }

    void WaterService::populateDefinitionMap(uint64_t entityId, const water::WaterGrid& grid)
    {
        auto& defMap = definitionMaps[entityId];
        defMap.clear();

        for (const auto* tile : grid.getAllTiles())
        {
            if (!tile) continue;
            water::WaterTileDefinition def;
            def.waterHeight = tile->waterHeight;
            def.waveIntensity = tile->waveIntensity;
            def.physicsEnabled = tile->physicsEnabled;
            defMap.addDefinition(tile->coord, def);
        }
    }
}
