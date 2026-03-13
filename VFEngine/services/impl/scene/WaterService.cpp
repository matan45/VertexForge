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
#include "../../events/project/SceneEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"

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
        dispatcher.unregisterCommandHandler<events::water::AddWaterTileCommand>();
        dispatcher.unregisterCommandHandler<events::water::RemoveWaterTileCommand>();
        dispatcher.unregisterCommandHandler<events::water::SetWaterTileHeightCommand>();
        dispatcher.unregisterCommandHandler<events::water::SetWaterGlobalSettingsCommand>();
        dispatcher.unregisterCommandHandler<events::water::RebuildWaterFromComponentsCommand>();
        dispatcher.unregisterCommandHandler<events::water::RemapWaterEntitiesCommand>();
        dispatcher.unregisterCommandHandler<events::water::SaveWaterCommand>();
        dispatcher.unregisterCommandHandler<events::water::LoadWaterCommand>();

        dispatcher.unregisterQueryHandler<events::water::GetWaterDataQuery>();
        dispatcher.unregisterQueryHandler<events::water::IsPositionInWaterQuery>();
        dispatcher.unregisterQueryHandler<events::water::GetWaterHeightAtQuery>();
        dispatcher.unregisterQueryHandler<events::water::HasWaterComponentQuery>();
        dispatcher.unregisterQueryHandler<events::water::GetWaterTileDataQuery>();
        dispatcher.unregisterQueryHandler<events::water::HasWaterTileComponentQuery>();

        dispatcher.unregisterCommandHandler<events::water::SetOceanFFTEnabledCommand>();
        dispatcher.unregisterCommandHandler<events::water::SetOceanFFTConfigCommand>();
        dispatcher.unregisterQueryHandler<events::water::GetOceanFFTConfigQuery>();
        dispatcher.unregisterQueryHandler<events::water::IsOceanFFTEnabledQuery>();

        dispatcher.unregisterCommandHandler<events::water::SetWaterStreamingEnabledCommand>();
        dispatcher.unregisterCommandHandler<events::water::SetWaterStreamingConfigCommand>();
        dispatcher.unregisterQueryHandler<events::water::GetWaterStreamingConfigQuery>();
        dispatcher.unregisterQueryHandler<events::water::IsWaterStreamingEnabledQuery>();
        dispatcher.unregisterQueryHandler<events::water::GetWaterStreamingStatsQuery>();

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
        registerOceanFFTHandlers(dispatcher);
        registerStreamingHandlers(dispatcher);

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

        dispatcher.registerCommandHandler<events::water::AddWaterTileCommand>(
            [this](const events::water::AddWaterTileCommand& cmd)
            {
                return addTile(cmd.waterEntity, cmd.tileX, cmd.tileZ);
            });

        dispatcher.registerCommandHandler<events::water::RemoveWaterTileCommand>(
            [this](const events::water::RemoveWaterTileCommand& cmd)
            {
                return removeTile(cmd.waterEntity, cmd.tileX, cmd.tileZ);
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

        dispatcher.registerCommandHandler<events::water::SaveWaterCommand>(
            [this](const events::water::SaveWaterCommand& cmd)
            {
                return saveWater(cmd.waterEntity, cmd.path);
            });

        dispatcher.registerCommandHandler<events::water::LoadWaterCommand>(
            [this](const events::water::LoadWaterCommand& cmd)
            {
                return loadWater(cmd.path);
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

        dispatcher.registerQueryHandler<events::water::GetWaterTileDataQuery>(
            [this](const events::water::GetWaterTileDataQuery& query) -> std::optional<WaterTileData>
            {
                if (!query.entity.isValid())
                    return std::nullopt;

                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity ent = internal::fromHandle(query.entity);
                if (!registry.valid(ent) || !registry.all_of<components::WaterTileComponent>(ent))
                    return std::nullopt;

                const auto& comp = registry.get<components::WaterTileComponent>(ent);
                WaterTileData data;
                data.tileX = comp.tileX;
                data.tileZ = comp.tileZ;
                data.waterHeight = comp.waterHeight;
                data.waveIntensity = comp.waveIntensity;
                data.physicsEnabled = comp.physicsEnabled;
                data.isVisible = comp.isVisible;
                return data;
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

    void WaterService::registerOceanFFTHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::water::SetOceanFFTEnabledCommand>(
            [this](const events::water::SetOceanFFTEnabledCommand& cmd)
            {
                oceanFFTEnabled = cmd.enabled;
                oceanConfig.enabled = cmd.enabled;
                oceanConfigVersion++;

                events::water::OceanFFTConfigChangedNotification notification;
                notification.config = oceanConfig;
                events::EventDispatcher::instance().publish(notification);
            });

        dispatcher.registerCommandHandler<events::water::SetOceanFFTConfigCommand>(
            [this](const events::water::SetOceanFFTConfigCommand& cmd)
            {
                oceanConfig = cmd.config;
                oceanFFTEnabled = cmd.config.enabled;
                oceanConfigVersion++;

                events::water::OceanFFTConfigChangedNotification notification;
                notification.config = oceanConfig;
                events::EventDispatcher::instance().publish(notification);
            });

        dispatcher.registerQueryHandler<events::water::GetOceanFFTConfigQuery>(
            [this](const events::water::GetOceanFFTConfigQuery&)
            {
                return oceanConfig;
            });

        dispatcher.registerQueryHandler<events::water::IsOceanFFTEnabledQuery>(
            [this](const events::water::IsOceanFFTEnabledQuery&)
            {
                return oceanFFTEnabled;
            });
    }

    void WaterService::registerStreamingHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::water::SetWaterStreamingEnabledCommand>(
            [this](const events::water::SetWaterStreamingEnabledCommand& cmd)
            {
                auto it = waterStreamers.find(cmd.waterEntity.id);
                if (it != waterStreamers.end() && it->second)
                    it->second->setEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::water::SetWaterStreamingConfigCommand>(
            [this](const events::water::SetWaterStreamingConfigCommand& cmd)
            {
                auto it = waterStreamers.find(cmd.waterEntity.id);
                if (it != waterStreamers.end() && it->second)
                {
                    water::WaterStreamingConfig config;
                    config.loadRadius = cmd.loadRadius;
                    config.unloadRadius = cmd.unloadRadius;
                    config.maxLoadsPerFrame = cmd.maxLoadsPerFrame;
                    config.maxUnloadsPerFrame = cmd.maxUnloadsPerFrame;
                    it->second->setConfig(config);
                }
            });

        dispatcher.registerQueryHandler<events::water::GetWaterStreamingConfigQuery>(
            [this](const events::water::GetWaterStreamingConfigQuery& query)
            {
                auto it = waterStreamers.find(query.waterEntity.id);
                if (it != waterStreamers.end() && it->second)
                {
                    const auto& cfg = it->second->getConfig();
                    events::water::WaterStreamingConfigData data;
                    data.loadRadius = cfg.loadRadius;
                    data.unloadRadius = cfg.unloadRadius;
                    data.maxLoadsPerFrame = cfg.maxLoadsPerFrame;
                    data.maxUnloadsPerFrame = cfg.maxUnloadsPerFrame;
                    return data;
                }
                return events::water::WaterStreamingConfigData{};
            });

        dispatcher.registerQueryHandler<events::water::IsWaterStreamingEnabledQuery>(
            [this](const events::water::IsWaterStreamingEnabledQuery& query)
            {
                auto it = waterStreamers.find(query.waterEntity.id);
                if (it != waterStreamers.end() && it->second)
                    return it->second->isEnabled();
                return false;
            });

        dispatcher.registerQueryHandler<events::water::GetWaterStreamingStatsQuery>(
            [this](const events::water::GetWaterStreamingStatsQuery& query)
                -> std::pair<uint32_t, uint32_t>
            {
                uint32_t loaded = 0;
                uint32_t total = 0;

                auto gridIt = waterGrids.find(query.waterEntity.id);
                if (gridIt != waterGrids.end())
                    loaded = static_cast<uint32_t>(gridIt->second->getTileCount());

                auto defIt = definitionMaps.find(query.waterEntity.id);
                if (defIt != definitionMaps.end())
                    total = static_cast<uint32_t>(defIt->second.size());

                return {loaded, total};
            });

        dispatcher.registerCommandHandler<events::water::LoadAllWaterTilesCommand>(
            [this](const events::water::LoadAllWaterTilesCommand& cmd)
            {
                loadAllWaterTiles(cmd.waterEntity);
            });
    }

}
