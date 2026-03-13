#include "WaterService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "water/WaterGrid.hpp"
#include "water/WaterTile.hpp"
#include "water/WaterTypes.hpp"
#include "../../data/EntityConversion.hpp"

namespace services
{
    std::optional<WaterData> WaterService::getWaterData(EntityHandle entity) const
    {
        if (!entity.isValid())
            return std::nullopt;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return std::nullopt;

        if (!registry.all_of<components::WaterComponent>(ent))
            return std::nullopt;

        const auto& comp = registry.get<components::WaterComponent>(ent);

        WaterData data;
        data.defaultWaterHeight = comp.defaultWaterHeight;
        data.defaultWaveIntensity = comp.defaultWaveIntensity;
        data.shallowColor = comp.shallowColor;
        data.deepColor = comp.deepColor;
        data.physicsEnabled = comp.physicsEnabled;
        data.isActive = comp.isActive;
        data.visibleTileCount = comp.visibleTileCount;
        data.savePath = comp.savePath;

        auto gridIt = waterGrids.find(entity.id);
        if (gridIt != waterGrids.end())
        {
            auto& grid = *gridIt->second;
            data.worldTileSize = grid.getConfig().worldTileSize;
            data.tileCount = static_cast<uint32_t>(grid.getTileCount());
            data.activeTileCount = static_cast<uint32_t>(grid.getTileCount());
            int32_t minX, minZ, maxX, maxZ;
            grid.computeBounds(minX, minZ, maxX, maxZ);
            data.gridMinX = minX;
            data.gridMinZ = minZ;
            data.gridMaxX = maxX;
            data.gridMaxZ = maxZ;
        }
        else
        {
            data.gridMinX = comp.gridMinX;
            data.gridMinZ = comp.gridMinZ;
            data.gridMaxX = comp.gridMaxX;
            data.gridMaxZ = comp.gridMaxZ;
            data.tileCount = comp.activeTileCount;
            data.activeTileCount = comp.activeTileCount;
        }

        return data;
    }

    bool WaterService::hasWaterComponent(EntityHandle entity) const
    {
        if (!entity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return false;

        return registry.all_of<components::WaterComponent>(ent);
    }

    bool WaterService::hasWaterTileComponent(EntityHandle entity) const
    {
        if (!entity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return false;

        return registry.all_of<components::WaterTileComponent>(ent);
    }

    bool WaterService::isPositionInWater(const glm::vec3& worldPos) const
    {
        // Check if position is within any water grid tile
        bool inTile = false;
        for (const auto& [entityId, grid] : waterGrids)
        {
            // Check tile existence (XZ bounds)
            int32_t tileX = static_cast<int32_t>(std::floor(worldPos.x / grid->getConfig().worldTileSize));
            int32_t tileZ = static_cast<int32_t>(std::floor(worldPos.z / grid->getConfig().worldTileSize));
            if (grid->getTile(water::TileCoord(tileX, tileZ)))
            {
                inTile = true;
                break;
            }
        }
        if (!inTile)
            return false;

        // Use getWaterHeightAt which includes ocean displacement
        float waterHeight = getWaterHeightAt(glm::vec2(worldPos.x, worldPos.z));
        return worldPos.y <= waterHeight;
    }

    float WaterService::getWaterHeightAt(const glm::vec2& worldXZ) const
    {
        if (waterGrids.empty())
            return 0.0f;

        float maxHeight = -std::numeric_limits<float>::max();
        for (const auto& [entityId, grid] : waterGrids)
        {
            float h = grid->getWaterHeightAt(worldXZ);
            if (h > maxHeight)
                maxHeight = h;
        }

        // Add ocean FFT displacement if active
        if (oceanFFTEnabled && oceanHeightSampler)
        {
            maxHeight += oceanHeightSampler(worldXZ);
        }

        return maxHeight;
    }

    void WaterService::setWaterTileHeight(EntityHandle waterEntity,
                                          int32_t tileX, int32_t tileZ, float height)
    {
        auto gridIt = waterGrids.find(waterEntity.id);
        if (gridIt == waterGrids.end())
            return;

        water::WaterTile* tile = gridIt->second->getTile(water::TileCoord(tileX, tileZ));
        if (!tile)
            return;

        tile->updateHeight(height, gridIt->second->getConfig().worldTileSize);

        scene::Entity parent(internal::fromHandle(waterEntity));
        for (auto& child : parent.getChildren())
        {
            if (!child.hasComponent<components::WaterTileComponent>())
                continue;
            auto& comp = child.getComponent<components::WaterTileComponent>();
            if (comp.tileX == tileX && comp.tileZ == tileZ)
            {
                comp.waterHeight = height;
                break;
            }
        }
    }

    water::WaterGlobalSettings WaterService::getWaterGlobalSettings() const
    {
        if (!globalSettingsDirty)
            return cachedGlobalSettings;

        cachedGlobalSettings = water::WaterGlobalSettings{};

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WaterComponent>();
        for (auto entity : view)
        {
            const auto& comp = view.get<components::WaterComponent>(entity);
            cachedGlobalSettings.density = comp.globalDensity;
            cachedGlobalSettings.drag = comp.globalDrag;
            cachedGlobalSettings.buoyancyStrength = comp.globalBuoyancyStrength;
            cachedGlobalSettings.waveSpeed = comp.waveSpeed;
            cachedGlobalSettings.waveAmplitude = comp.waveAmplitude;
            cachedGlobalSettings.waveFrequency = comp.waveFrequency;
            cachedGlobalSettings.shallowColor = comp.shallowColor;
            cachedGlobalSettings.deepColor = comp.deepColor;
            cachedGlobalSettings.maxVisibleDepth = comp.maxVisibleDepth;
            cachedGlobalSettings.fresnelPower = comp.fresnelPower;
            cachedGlobalSettings.dudvTiling = comp.dudvTiling;
            cachedGlobalSettings.dudvStrength = comp.dudvStrength;
            cachedGlobalSettings.waveDirectionDegrees = comp.waveDirectionDegrees;
            break;
        }

        globalSettingsDirty = false;
        return cachedGlobalSettings;
    }

    water::WaterTileConfig WaterService::getWaterTileConfig() const
    {
        if (!waterGrids.empty())
        {
            return waterGrids.begin()->second->getConfig();
        }
        return water::WaterTileConfig{};
    }

    void WaterService::setWaterGlobalSettings(EntityHandle waterEntity,
                                              const WaterGlobalSettingsData& settings)
    {
        if (!waterEntity.isValid())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(waterEntity);

        if (!registry.valid(ent) || !registry.all_of<components::WaterComponent>(ent))
            return;

        auto& comp = registry.get<components::WaterComponent>(ent);
        comp.globalDensity = settings.density;
        comp.globalDrag = settings.drag;
        comp.globalBuoyancyStrength = settings.buoyancyStrength;
        comp.waveSpeed = settings.waveSpeed;
        comp.waveAmplitude = settings.waveAmplitude;
        comp.waveFrequency = settings.waveFrequency;
        comp.shallowColor = settings.shallowColor;
        comp.deepColor = settings.deepColor;
        comp.maxVisibleDepth = settings.maxVisibleDepth;
        comp.fresnelPower = settings.fresnelPower;
        comp.dudvTiling = settings.dudvTiling;
        comp.dudvStrength = settings.dudvStrength;
        comp.waveDirectionDegrees = settings.waveDirectionDegrees;

        globalSettingsDirty = true;
    }
}
