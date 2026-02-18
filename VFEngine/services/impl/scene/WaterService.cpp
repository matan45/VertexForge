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
#include "../../events/SceneEvents.hpp"
#include "../../events/PhysicsEvents.hpp"
#include "print/EditorLogger.hpp"

namespace services
{
    WaterService::WaterService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
    {
    }

    WaterService::~WaterService()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.unregisterCommandHandler<events::water::CreateWaterCommand>();
        dispatcher.unregisterCommandHandler<events::water::DeleteWaterCommand>();
        dispatcher.unregisterCommandHandler<events::water::SetWaterTileHeightCommand>();
        dispatcher.unregisterCommandHandler<events::water::SetWaterGlobalSettingsCommand>();
        dispatcher.unregisterCommandHandler<events::water::RebuildWaterFromComponentsCommand>();
        dispatcher.unregisterCommandHandler<events::water::RemapWaterEntitiesCommand>();

        dispatcher.unregisterQueryHandler<events::water::GetWaterDataQuery>();
        dispatcher.unregisterQueryHandler<events::water::IsPositionInWaterQuery>();
        dispatcher.unregisterQueryHandler<events::water::GetWaterHeightAtQuery>();
        dispatcher.unregisterQueryHandler<events::water::HasWaterComponentQuery>();
        dispatcher.unregisterQueryHandler<events::water::HasWaterTileComponentQuery>();

        if (triggerEnterSubscription && triggerEnterSubscription->isValid())
        {
            dispatcher.unsubscribe(*triggerEnterSubscription);
        }

        if (triggerExitSubscription && triggerExitSubscription->isValid())
        {
            dispatcher.unsubscribe(*triggerExitSubscription);
        }

        if (entityDeletedSubscription && entityDeletedSubscription->isValid())
        {
            dispatcher.unsubscribe(*entityDeletedSubscription);
        }

        if (sceneClearedSubscription && sceneClearedSubscription->isValid())
        {
            dispatcher.unsubscribe(*sceneClearedSubscription);
        }

        waterGrids.clear();
    }

    void WaterService::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        registerWaterCoreHandlers(dispatcher);
        registerWaterQueryHandlers(dispatcher);

        auto token = dispatcher.subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& notification)
            {
                onEntityDeleted(notification.entity);
            });
        entityDeletedSubscription = std::make_unique<events::SubscriptionToken>(token);

        auto sceneToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                onSceneCleared();
            });
        sceneClearedSubscription = std::make_unique<events::SubscriptionToken>(sceneToken);

        // Physics trigger subscriptions for water sensor bodies
        auto enterToken = dispatcher.subscribe<events::physics::TriggerEnterNotification>(
            [this](const events::physics::TriggerEnterNotification& notification)
            {
                if (!hasWaterTileComponent(notification.triggerEntity))
                    return;

                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity ent = internal::fromHandle(notification.triggerEntity);
                if (!registry.valid(ent) || !registry.all_of<components::WaterTileComponent>(ent))
                    return;

                const auto& tileComp = registry.get<components::WaterTileComponent>(ent);

                events::water::WaterTileEnteredNotification waterNotif;
                waterNotif.entity = notification.otherEntity;
                waterNotif.tileX = tileComp.tileX;
                waterNotif.tileZ = tileComp.tileZ;
                events::EventDispatcher::instance().publish(waterNotif);

                entitiesInWater.insert(notification.otherEntity);
            });
        triggerEnterSubscription = std::make_unique<events::SubscriptionToken>(enterToken);

        auto exitToken = dispatcher.subscribe<events::physics::TriggerExitNotification>(
            [this](const events::physics::TriggerExitNotification& notification)
            {
                if (!hasWaterTileComponent(notification.triggerEntity))
                    return;

                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity ent = internal::fromHandle(notification.triggerEntity);
                if (!registry.valid(ent) || !registry.all_of<components::WaterTileComponent>(ent))
                    return;

                const auto& tileComp = registry.get<components::WaterTileComponent>(ent);

                events::water::WaterTileExitedNotification waterNotif;
                waterNotif.entity = notification.otherEntity;
                waterNotif.tileX = tileComp.tileX;
                waterNotif.tileZ = tileComp.tileZ;
                events::EventDispatcher::instance().publish(waterNotif);

                entitiesInWater.erase(notification.otherEntity);
            });
        triggerExitSubscription = std::make_unique<events::SubscriptionToken>(exitToken);
    }

    void WaterService::registerWaterCoreHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::water::CreateWaterCommand>(
            [this](const events::water::CreateWaterCommand& cmd)
            {
                return createWater(cmd.config);
            });

        dispatcher.registerCommandHandler<events::water::DeleteWaterCommand>(
            [this](const events::water::DeleteWaterCommand& cmd)
            {
                return deleteWater(cmd.waterEntity);
            });

        dispatcher.registerCommandHandler<events::water::SetWaterTileHeightCommand>(
            [this](const events::water::SetWaterTileHeightCommand& cmd)
            {
                setWaterTileHeight(cmd.waterEntity, cmd.tileX, cmd.tileZ, cmd.waterHeight);
            });

        dispatcher.registerCommandHandler<events::water::SetWaterGlobalSettingsCommand>(
            [this](const events::water::SetWaterGlobalSettingsCommand& cmd)
            {
                setWaterGlobalSettings(cmd.waterEntity, cmd.settings);
            });

        dispatcher.registerCommandHandler<events::water::RebuildWaterFromComponentsCommand>(
            [this](const events::water::RebuildWaterFromComponentsCommand&)
            {
                rebuildWaterFromComponents();
            });

        dispatcher.registerCommandHandler<events::water::RemapWaterEntitiesCommand>(
            [this](const events::water::RemapWaterEntitiesCommand&)
            {
                remapWaterEntities();
            });
    }

    void WaterService::registerWaterQueryHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerQueryHandler<events::water::GetWaterDataQuery>(
            [this](const events::water::GetWaterDataQuery& query)
            {
                return getWaterData(query.entity);
            });

        dispatcher.registerQueryHandler<events::water::IsPositionInWaterQuery>(
            [this](const events::water::IsPositionInWaterQuery& query)
            {
                return isPositionInWater(query.position);
            });

        dispatcher.registerQueryHandler<events::water::GetWaterHeightAtQuery>(
            [this](const events::water::GetWaterHeightAtQuery& query)
            {
                return getWaterHeightAt(query.worldXZ);
            });

        dispatcher.registerQueryHandler<events::water::HasWaterComponentQuery>(
            [this](const events::water::HasWaterComponentQuery& query)
            {
                return hasWaterComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::water::HasWaterTileComponentQuery>(
            [this](const events::water::HasWaterTileComponentQuery& query)
            {
                return hasWaterTileComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::water::GetWaterEntityQuery>(
            [this](const events::water::GetWaterEntityQuery&)
            {
                if (waterGrids.empty())
                    return EntityHandle{};
                return EntityHandle{waterGrids.begin()->first};
            });

        dispatcher.registerQueryHandler<events::water::GetWaterGlobalSettingsQuery>(
            [this](const events::water::GetWaterGlobalSettingsQuery&)
            {
                auto settings = getWaterGlobalSettings();
                WaterGlobalSettingsData data;
                data.density = settings.density;
                data.drag = settings.drag;
                data.buoyancyStrength = settings.buoyancyStrength;
                data.waveSpeed = settings.waveSpeed;
                data.waveAmplitude = settings.waveAmplitude;
                data.waveFrequency = settings.waveFrequency;
                data.shallowColor = settings.shallowColor;
                data.deepColor = settings.deepColor;
                data.maxVisibleDepth = settings.maxVisibleDepth;
                data.fresnelPower = settings.fresnelPower;
                data.dudvTiling = settings.dudvTiling;
                data.dudvStrength = settings.dudvStrength;
                data.waveDirectionDegrees = settings.waveDirectionDegrees;
                return data;
            });
    }

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
        data.gridMinX = comp.gridMinX;
        data.gridMinZ = comp.gridMinZ;
        data.gridMaxX = comp.gridMaxX;
        data.gridMaxZ = comp.gridMaxZ;
        data.shallowColor = comp.shallowColor;
        data.deepColor = comp.deepColor;
        data.physicsEnabled = comp.physicsEnabled;
        data.isActive = comp.isActive;
        data.tileCount = static_cast<uint32_t>(
            (comp.gridMaxX - comp.gridMinX + 1) * (comp.gridMaxZ - comp.gridMinZ + 1));
        data.activeTileCount = comp.activeTileCount;
        data.visibleTileCount = comp.visibleTileCount;

        auto gridIt = waterGrids.find(entity.id);
        if (gridIt != waterGrids.end())
        {
            data.worldTileSize = gridIt->second->getConfig().worldTileSize;
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

    std::vector<water::WaterTile*> WaterService::getVisibleWaterTiles(
        const math::Frustum& frustum,
        const glm::vec3& /*cameraPosition*/)
    {
        std::vector<water::WaterTile*> result;

        for (auto& [entityId, grid] : waterGrids)
        {
            auto visibleTiles = grid->getVisibleTiles(frustum);
            result.insert(result.end(), visibleTiles.begin(), visibleTiles.end());
        }

        return result;
    }

    bool WaterService::isPositionInWater(const glm::vec3& worldPos) const
    {
        for (const auto& [entityId, grid] : waterGrids)
        {
            if (grid->isPositionInWater(worldPos))
                return true;
        }
        return false;
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
