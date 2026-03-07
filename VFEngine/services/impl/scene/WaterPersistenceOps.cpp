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
#include "../../events/project/ResourceEvents.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"

namespace services
{
    bool WaterService::saveWater(EntityHandle waterEntity, const std::string& path)
    {
        if (!waterEntity.isValid())
        {
            vfLogError("WaterService::saveWater: Invalid entity");
            return false;
        }

        auto gridIt = waterGrids.find(waterEntity.id);
        if (gridIt == waterGrids.end())
        {
            vfLogError("WaterService::saveWater: No water grid for entity {}", waterEntity.id);
            return false;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(waterEntity);
        if (!registry.valid(ent) || !registry.all_of<components::WaterComponent>(ent))
        {
            vfLogError("WaterService::saveWater: Entity missing WaterComponent");
            return false;
        }

        const auto& comp = registry.get<components::WaterComponent>(ent);

        // Build global settings from component
        water::WaterGlobalSettings settings;
        settings.density = comp.globalDensity;
        settings.drag = comp.globalDrag;
        settings.buoyancyStrength = comp.globalBuoyancyStrength;
        settings.waveSpeed = comp.waveSpeed;
        settings.waveAmplitude = comp.waveAmplitude;
        settings.waveFrequency = comp.waveFrequency;
        settings.shallowColor = comp.shallowColor;
        settings.deepColor = comp.deepColor;
        settings.maxVisibleDepth = comp.maxVisibleDepth;
        settings.fresnelPower = comp.fresnelPower;
        settings.dudvTiling = comp.dudvTiling;
        settings.dudvStrength = comp.dudvStrength;
        settings.waveDirectionDegrees = comp.waveDirectionDegrees;

        int32_t boundsMinX, boundsMinZ, boundsMaxX, boundsMaxZ;
        gridIt->second->computeBounds(boundsMinX, boundsMinZ, boundsMaxX, boundsMaxZ);

        bool success = water::WaterSerializer::save(
            path,
            *gridIt->second,
            settings,
            boundsMinX, boundsMinZ,
            boundsMaxX, boundsMaxZ,
            comp.physicsEnabled);

        if (!success)
        {
            vfLogError("WaterService::saveWater: Serialization failed for {}", path);
            return false;
        }

        // Update savePath and bounds on the component
        auto& mutableComp = registry.get<components::WaterComponent>(ent);
        mutableComp.savePath = path;
        mutableComp.gridMinX = boundsMinX;
        mutableComp.gridMinZ = boundsMinZ;
        mutableComp.gridMaxX = boundsMaxX;
        mutableComp.gridMaxZ = boundsMaxZ;
        mutableComp.activeTileCount = static_cast<uint32_t>(gridIt->second->getTileCount());

        events::water::WaterSavedNotification notification;
        notification.waterEntity = waterEntity;
        notification.path = path;
        events::EventDispatcher::instance().publish(notification);

        events::resource::AssetSavedNotification assetNotif;
        assetNotif.filePath = path;
        events::EventDispatcher::instance().publish(assetNotif);

        vfLogInfo("WaterService: Saved water to {}", path);
        return true;
    }

    EntityHandle WaterService::loadWater(const std::string& path)
    {
        water::WaterLoadResult loadResult;
        if (!water::WaterSerializer::loadAll(path, loadResult))
        {
            vfLogError("WaterService::loadWater: Failed to load from {}", path);
            return {};
        }

        const auto& header = loadResult.header;

        water::WaterTileConfig tileConfig;
        tileConfig.worldTileSize = header.worldTileSize;

        auto grid = std::make_unique<water::WaterGrid>(tileConfig, 0.0f);

        // Create only the tiles that exist in the file (sparse)
        for (const auto& tileData : loadResult.tiles)
        {
            water::WaterTile* tile = grid->getOrCreateTile(water::TileCoord(tileData.tileX, tileData.tileZ));
            if (tile)
            {
                tile->updateHeight(tileData.waterHeight, tileConfig.worldTileSize);
                tile->waveIntensity = tileData.waveIntensity;
                tile->physicsEnabled = tileData.physicsEnabled;
            }
        }

        // Create entity hierarchy
        scene::Entity parentEntity("Water");
        sceneGraph->addChild(sceneGraph->GetRoot(), parentEntity);

        auto& waterComp = parentEntity.addComponent<components::WaterComponent>();
        waterComp.worldTileSize = header.worldTileSize;

        int32_t loadedMinX, loadedMinZ, loadedMaxX, loadedMaxZ;
        grid->computeBounds(loadedMinX, loadedMinZ, loadedMaxX, loadedMaxZ);
        waterComp.gridMinX = loadedMinX;
        waterComp.gridMinZ = loadedMinZ;
        waterComp.gridMaxX = loadedMaxX;
        waterComp.gridMaxZ = loadedMaxZ;
        waterComp.physicsEnabled = header.physicsEnabled;
        waterComp.isActive = true;
        waterComp.savePath = path;

        // Apply global settings
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

        // Compute default height from first tile (or 0)
        if (!loadResult.tiles.empty())
        {
            waterComp.defaultWaterHeight = loadResult.tiles[0].waterHeight;
            waterComp.defaultWaveIntensity = loadResult.tiles[0].waveIntensity;
        }

        waterComp.activeTileCount = static_cast<uint32_t>(grid->getTileCount());
        waterComp.visibleTileCount = 0;

        EntityHandle parentHandle = internal::toHandle(parentEntity.getHandle());

        createTileEntities(parentHandle, *grid);

        waterGrids[parentHandle.id] = std::move(grid);
        globalSettingsDirty = true;

        events::water::WaterLoadedNotification notification;
        notification.waterEntity = parentHandle;
        notification.path = path;
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("WaterService: Loaded water with {} tiles from {}", header.tileCount, path);
        return parentHandle;
    }
}
