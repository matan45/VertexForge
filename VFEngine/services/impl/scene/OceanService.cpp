#include "OceanService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/OceanComponents.hpp"
#include "components/Components.hpp"
#include "water/OceanSerializer.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/OceanEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/weather/WeatherEvents.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"
#include "../../../utilities/water/WaterTileGrid.hpp"
#include "../../../utilities/water/BuoyancySampling.hpp"
#include <cmath>

namespace services
{
    OceanService::OceanService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
    {
    }

    OceanService::~OceanService()
    {
        physicsProvider = nullptr;
        sceneGraph.reset();

        auto& dispatcher = events::EventDispatcher::instance();

        // Unsubscribe notifications first to prevent callbacks during teardown
        if (entityDeletedSubscription && entityDeletedSubscription->isValid())
        {
            dispatcher.unsubscribe(*entityDeletedSubscription);
            entityDeletedSubscription.reset();
        }
        if (sceneClearedSubscription && sceneClearedSubscription->isValid())
        {
            dispatcher.unsubscribe(*sceneClearedSubscription);
            sceneClearedSubscription.reset();
        }

        if (sectorActivatedSub && sectorActivatedSub->isValid())
        {
            dispatcher.unsubscribe(*sectorActivatedSub);
            sectorActivatedSub.reset();
        }
        if (sectorDeactivatedSub && sectorDeactivatedSub->isValid())
        {
            dispatcher.unsubscribe(*sectorDeactivatedSub);
            sectorDeactivatedSub.reset();
        }
        if (worldLoadedSub && worldLoadedSub->isValid())
        {
            dispatcher.unsubscribe(*worldLoadedSub);
            worldLoadedSub.reset();
        }

        waterTileGrid.reset();

        dispatcher.unregisterCommandHandler<events::ocean::CreateOceanCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::DeleteOceanCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SetOceanVisualSettingsCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SetOceanPhysicsSettingsCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SetOceanFFTConfigCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SaveOceanCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::LoadOceanCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::RebuildOceanFromComponentsCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::UpdateOceanCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SetOceanSeaStateCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SetOceanWeatherDrivenCommand>();

        dispatcher.unregisterQueryHandler<events::ocean::GetOceanEntityQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetOceanDataQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetOceanVisualSettingsQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::HasOceanComponentQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetOceanFFTConfigQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::IsOceanFFTEnabledQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetOceanHeightAtQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetWaterDepthAtQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetShoreDepthFieldStatusQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::IsPositionInOceanQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::IsEntityInWaterQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetOceanSeaStateQuery>();

        // VK-1607
        dispatcher.unregisterCommandHandler<events::ocean::CreateWaterBodyCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::AddWaterBodyComponentCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::RemoveWaterBodyComponentCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SetWaterBodyDataCommand>();
        dispatcher.unregisterQueryHandler<events::ocean::GetWaterBodyDataQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::HasWaterBodyComponentQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetWaterBodiesQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::HasAnyWaterQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetWaterSurfaceAtQuery>();
    }

    void OceanService::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        registerOceanCoreHandlers(dispatcher);
        registerOceanQueryHandlers(dispatcher);
        registerOceanFFTHandlers(dispatcher);
        registerWaterBodyHandlers(dispatcher);

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

        // Sector-driven water tile streaming subscriptions
        auto activatedToken = dispatcher.subscribe<events::world::SectorActivatedNotification>(
            [this](const events::world::SectorActivatedNotification& notif)
            {
                onSectorActivated(notif.coord, notif.sectorConfig);
            });
        sectorActivatedSub = std::make_unique<events::SubscriptionToken>(activatedToken);

        auto deactivatedToken = dispatcher.subscribe<events::world::SectorDeactivatedNotification>(
            [this](const events::world::SectorDeactivatedNotification& notif)
            {
                onSectorDeactivated(notif.coord, notif.sectorConfig);
            });
        sectorDeactivatedSub = std::make_unique<events::SubscriptionToken>(deactivatedToken);

        auto worldLoadedToken = dispatcher.subscribe<events::world::WorldLoadedNotification>(
            [this](const events::world::WorldLoadedNotification&)
            {
                activateWaterTilesForLoadedSectors();
            });
        worldLoadedSub = std::make_unique<events::SubscriptionToken>(worldLoadedToken);

        // VK-1605: the shore-depth field is baked from a terrain snapshot, so it has to be redone
        // whenever the terrain itself changes shape or extent. Note there is no "heights sculpted"
        // notification in the engine — an in-editor sculpt only reaches the field on the next
        // camera-driven rebake (or by toggling shoaling off/on).
        auto terrainCreatedToken = dispatcher.subscribe<events::terrain::TerrainCreatedNotification>(
            [this](const events::terrain::TerrainCreatedNotification&)
            {
                shoreFieldRebakeRequested = true;
            });
        terrainChangedSub = std::make_unique<events::SubscriptionToken>(terrainCreatedToken);

        auto terrainLoadedToken = dispatcher.subscribe<events::terrain::TerrainLoadedNotification>(
            [this](const events::terrain::TerrainLoadedNotification&)
            {
                shoreFieldRebakeRequested = true;
            });
        terrainLoadedSub = std::make_unique<events::SubscriptionToken>(terrainLoadedToken);
    }

    void OceanService::registerOceanCoreHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::ocean::CreateOceanCommand>(
            [this](const events::ocean::CreateOceanCommand& cmd)
            {
                return createOcean(cmd.config);
            });

        dispatcher.registerCommandHandler<events::ocean::DeleteOceanCommand>(
            [this](const events::ocean::DeleteOceanCommand& cmd)
            {
                return deleteOcean(cmd.oceanEntity);
            });

        dispatcher.registerCommandHandler<events::ocean::SetOceanVisualSettingsCommand>(
            [this](const events::ocean::SetOceanVisualSettingsCommand& cmd)
            {
                if (!cmd.oceanEntity.isValid())
                    return;

                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity ent = internal::fromHandle(cmd.oceanEntity);
                if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
                    return;

                auto& comp = registry.get<components::OceanComponent>(ent);
                comp.shallowColor = cmd.settings.shallowColor;
                comp.deepColor = cmd.settings.deepColor;
                comp.maxVisibleDepth = cmd.settings.maxVisibleDepth;
                comp.fresnelPower = cmd.settings.fresnelPower;
                comp.refractionStrength = cmd.settings.refractionStrength;
                comp.refractionChromatic = cmd.settings.refractionChromatic;
                comp.refractionDepthScale = cmd.settings.refractionDepthScale;
                comp.causticStrength = cmd.settings.causticStrength;
                comp.causticDepthFalloff = cmd.settings.causticDepthFalloff;
                comp.shoreFoamRange = cmd.settings.shoreFoamRange;
                comp.shoreFoamIntensity = cmd.settings.shoreFoamIntensity;
                comp.shoreBreakingStrength = cmd.settings.shoreBreakingStrength;
                comp.shoreWetRange = cmd.settings.shoreWetRange;
                comp.shoreWetDarkening = cmd.settings.shoreWetDarkening;
                comp.shoreWetRoughness = cmd.settings.shoreWetRoughness;
                // VK-1604
                comp.ssrEnabled = cmd.settings.ssrEnabled;
                comp.ssrIntensity = cmd.settings.ssrIntensity;
                comp.ssrMaxDistance = cmd.settings.ssrMaxDistance;
                comp.ssrThickness = cmd.settings.ssrThickness;
                comp.ssrMaxSteps = cmd.settings.ssrMaxSteps;
                comp.ssrDebugView = cmd.settings.ssrDebugView;
                comp.beerLambertEnabled = cmd.settings.beerLambertEnabled;
                comp.absorptionCoeff = cmd.settings.absorptionCoeff;
                comp.scatteringColor = cmd.settings.scatteringColor;
                comp.scatterCoeff = cmd.settings.scatterCoeff;
                comp.absorptionMaxDistance = cmd.settings.absorptionMaxDistance;
                comp.hexTilingEnabled = cmd.settings.hexTilingEnabled;
                comp.hexBandMask = cmd.settings.hexBandMask;
                comp.hexCellScale = cmd.settings.hexCellScale;
                comp.hexBlendContrast = cmd.settings.hexBlendContrast;
                // VK-1605
                comp.shoalingEnabled = cmd.settings.shoalingEnabled;
                comp.shoalingStrength = cmd.settings.shoalingStrength;
                comp.shoalingMinDepth = cmd.settings.shoalingMinDepth;
                comp.shoalingWavelengthScale = cmd.settings.shoalingWavelengthScale;
                comp.shoalingGamma = cmd.settings.shoalingGamma;
                comp.shoreEdgeFadeStart = cmd.settings.shoreEdgeFadeStart;
                comp.shoreWavesEnabled = cmd.settings.shoreWavesEnabled;
                comp.shoreWaveAmplitude = cmd.settings.shoreWaveAmplitude;
                comp.shoreWaveLength = cmd.settings.shoreWaveLength;
                comp.shoreWaveSpeed = cmd.settings.shoreWaveSpeed;
                comp.shoreWaveBreakDepth = cmd.settings.shoreWaveBreakDepth;
                comp.shoreWaveBreakRange = cmd.settings.shoreWaveBreakRange;
                comp.shoreWaveCrestFoam = cmd.settings.shoreWaveCrestFoam;
                comp.shoreWaveCrestFoamThreshold = cmd.settings.shoreWaveCrestFoamThreshold;
                comp.shoreWaveLean = cmd.settings.shoreWaveLean;
                // VK-1606
                comp.rippleSimEnabled = cmd.settings.rippleSimEnabled;
                comp.ripplePatchSize = cmd.settings.ripplePatchSize;
                comp.rippleWaveSpeed = cmd.settings.rippleWaveSpeed;
                comp.rippleDamping = cmd.settings.rippleDamping;
                comp.rippleHeightScale = cmd.settings.rippleHeightScale;
                comp.rippleNormalScale = cmd.settings.rippleNormalScale;
                comp.rippleFoamGain = cmd.settings.rippleFoamGain;
                comp.rippleFoamScale = cmd.settings.rippleFoamScale;
                comp.rippleFoamDecay = cmd.settings.rippleFoamDecay;
                comp.rippleEdgeFadeStart = cmd.settings.rippleEdgeFadeStart;
            });

        dispatcher.registerCommandHandler<events::ocean::SetOceanPhysicsSettingsCommand>(
            [this](const events::ocean::SetOceanPhysicsSettingsCommand& cmd)
            {
                if (!cmd.oceanEntity.isValid())
                    return;

                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity ent = internal::fromHandle(cmd.oceanEntity);
                if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
                    return;

                auto& comp = registry.get<components::OceanComponent>(ent);
                comp.density = cmd.settings.density;
                comp.drag = cmd.settings.drag;
                comp.buoyancyStrength = cmd.settings.buoyancyStrength;
                comp.physicsEnabled = cmd.settings.physicsEnabled;
            });

        dispatcher.registerCommandHandler<events::ocean::SaveOceanCommand>(
            [this](const events::ocean::SaveOceanCommand& cmd)
            {
                return saveOcean(cmd.oceanEntity, cmd.path);
            });

        dispatcher.registerCommandHandler<events::ocean::LoadOceanCommand>(
            [this](const events::ocean::LoadOceanCommand& cmd)
            {
                return loadOcean(cmd.path);
            });

        dispatcher.registerCommandHandler<events::ocean::RebuildOceanFromComponentsCommand>(
            [this](const events::ocean::RebuildOceanFromComponentsCommand&)
            {
                rebuildOceanFromComponents();
            });

        dispatcher.registerCommandHandler<events::ocean::UpdateOceanCommand>(
            [this](const events::ocean::UpdateOceanCommand& cmd)
            {
                update(cmd.deltaTime);
            });

        dispatcher.registerCommandHandler<events::ocean::SetOceanSeaStateCommand>(
            [this](const events::ocean::SetOceanSeaStateCommand& cmd)
            {
                setSeaState(cmd.beaufort, cmd.transitionSeconds);
            });

        dispatcher.registerCommandHandler<events::ocean::SetOceanWeatherDrivenCommand>(
            [this](const events::ocean::SetOceanWeatherDrivenCommand& cmd)
            {
                if (!cmd.oceanEntity.isValid())
                    return;

                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity ent = internal::fromHandle(cmd.oceanEntity);
                if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
                    return;

                auto& comp = registry.get<components::OceanComponent>(ent);
                comp.weatherDriven = cmd.enabled;
                comp.weatherResponse = cmd.response;
                // Force a reapply on the next update tick
                lastAppliedBeaufort = -1.0f;
                lastAppliedWindDirection = -10000.0f;
            });

        // VK-1606
        dispatcher.registerCommandHandler<events::ocean::AddWaterImpulseCommand>(
            [this](const events::ocean::AddWaterImpulseCommand& cmd)
            {
                queueWaterImpulse({cmd.positionXZ, cmd.radius, cmd.strength});
            });

        dispatcher.registerCommandHandler<events::ocean::SetWaterRippleEnabledCommand>(
            [this](const events::ocean::SetWaterRippleEnabledCommand& cmd)
            {
                setRippleSimEnabled(cmd.enabled);
            });
    }

    void OceanService::registerOceanQueryHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerQueryHandler<events::ocean::GetOceanEntityQuery>(
            [this](const events::ocean::GetOceanEntityQuery&)
            {
                return oceanEntity;
            });

        dispatcher.registerQueryHandler<events::ocean::GetOceanDataQuery>(
            [this](const events::ocean::GetOceanDataQuery& query)
            {
                return getOceanData(query.entity);
            });

        dispatcher.registerQueryHandler<events::ocean::GetOceanVisualSettingsQuery>(
            [this](const events::ocean::GetOceanVisualSettingsQuery& query)
            {
                return getOceanVisualSettings(query.entity);
            });

        dispatcher.registerQueryHandler<events::ocean::HasOceanComponentQuery>(
            [this](const events::ocean::HasOceanComponentQuery& query)
            {
                return hasOceanComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ocean::GetOceanHeightAtQuery>(
            [this](const events::ocean::GetOceanHeightAtQuery& query)
            {
                return getOceanHeightAt(query.worldXZ);
            });

        // VK-1605
        dispatcher.registerQueryHandler<events::ocean::GetWaterDepthAtQuery>(
            [this](const events::ocean::GetWaterDepthAtQuery& query)
            {
                return getWaterDepthAt(query.worldXZ);
            });

        dispatcher.registerQueryHandler<events::ocean::GetShoreDepthFieldStatusQuery>(
            [this](const events::ocean::GetShoreDepthFieldStatusQuery&)
            {
                return getShoreDepthFieldStatus();
            });

        dispatcher.registerQueryHandler<events::ocean::IsPositionInOceanQuery>(
            [this](const events::ocean::IsPositionInOceanQuery& query)
            {
                return isPositionInOcean(query.position);
            });

        dispatcher.registerQueryHandler<events::ocean::IsEntityInWaterQuery>(
            [this](const events::ocean::IsEntityInWaterQuery& query)
            {
                return isEntityInWater(query.entity);
            });

        dispatcher.registerQueryHandler<events::ocean::GetOceanSeaStateQuery>(
            [this](const events::ocean::GetOceanSeaStateQuery&)
            {
                return getSeaState();
            });

        // VK-1606
        dispatcher.registerQueryHandler<events::ocean::IsWaterRippleEnabledQuery>(
            [this](const events::ocean::IsWaterRippleEnabledQuery&)
            {
                return isRippleSimEnabled();
            });
    }

    void OceanService::registerOceanFFTHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::ocean::SetOceanFFTConfigCommand>(
            [this](const events::ocean::SetOceanFFTConfigCommand& cmd)
            {
                oceanConfig = cmd.config;
                oceanConfigVersion++;

                // Sync to component
                if (oceanEntity.isValid())
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    entt::entity ent = internal::fromHandle(oceanEntity);
                    if (registry.valid(ent) && registry.all_of<components::OceanComponent>(ent))
                    {
                        auto& comp = registry.get<components::OceanComponent>(ent);
                        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
                        {
                            comp.oceanBands[i].resolution = cmd.config.bands[i].resolution;
                            comp.oceanBands[i].patchSize = cmd.config.bands[i].patchSize;
                            comp.oceanBands[i].windSpeed = cmd.config.bands[i].windSpeed;
                            comp.oceanBands[i].windDirection = cmd.config.bands[i].windDirection;
                            comp.oceanBands[i].amplitude = cmd.config.bands[i].amplitude;
                            comp.oceanBands[i].choppiness = cmd.config.bands[i].choppiness;
                            comp.oceanBands[i].foamThreshold = cmd.config.bands[i].foamThreshold;
                            comp.oceanBands[i].displacementScale = cmd.config.bands[i].displacementScale;
                            comp.oceanBands[i].enabled = cmd.config.bands[i].enabled;
                            comp.oceanBands[i].foamPersistence = cmd.config.bands[i].foamPersistence;
                            comp.oceanBands[i].foamDecay = cmd.config.bands[i].foamDecay;
                        }
                        comp.oceanGravity = cmd.config.gravity;
                    }
                }

                events::ocean::OceanFFTConfigChangedNotification notification;
                notification.config = oceanConfig;
                events::EventDispatcher::instance().publish(notification);
            });

        dispatcher.registerQueryHandler<events::ocean::GetOceanFFTConfigQuery>(
            [this](const events::ocean::GetOceanFFTConfigQuery&)
            {
                return oceanConfig;
            });

        dispatcher.registerQueryHandler<events::ocean::IsOceanFFTEnabledQuery>(
            [this](const events::ocean::IsOceanFFTEnabledQuery&)
            {
                return oceanConfig.enabled;
            });
    }

    EntityHandle OceanService::createOcean(const OceanCreationData& config)
    {
        scene::Entity parentEntity("Ocean");
        sceneGraph->addChild(sceneGraph->GetRoot(), parentEntity);

        auto& comp = parentEntity.addComponent<components::OceanComponent>();
        comp.waterHeight = config.waterHeight;
        comp.physicsEnabled = config.physicsEnabled;
        comp.shallowColor = config.shallowColor;
        comp.deepColor = config.deepColor;
        comp.isActive = true;

        // Apply ocean FFT config
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            comp.oceanBands[i].resolution = config.oceanConfig.bands[i].resolution;
            comp.oceanBands[i].patchSize = config.oceanConfig.bands[i].patchSize;
            comp.oceanBands[i].windSpeed = config.oceanConfig.bands[i].windSpeed;
            comp.oceanBands[i].windDirection = config.oceanConfig.bands[i].windDirection;
            comp.oceanBands[i].amplitude = config.oceanConfig.bands[i].amplitude;
            comp.oceanBands[i].choppiness = config.oceanConfig.bands[i].choppiness;
            comp.oceanBands[i].foamThreshold = config.oceanConfig.bands[i].foamThreshold;
            comp.oceanBands[i].displacementScale = config.oceanConfig.bands[i].displacementScale;
            comp.oceanBands[i].enabled = config.oceanConfig.bands[i].enabled;
            comp.oceanBands[i].foamPersistence = config.oceanConfig.bands[i].foamPersistence;
            comp.oceanBands[i].foamDecay = config.oceanConfig.bands[i].foamDecay;
        }
        comp.oceanGravity = config.oceanConfig.gravity;

        oceanEntity = internal::toHandle(parentEntity.getHandle());

        // Set ocean config
        oceanConfig = config.oceanConfig;
        oceanConfigVersion++;

        events::ocean::OceanCreatedNotification notification;
        notification.oceanEntity = oceanEntity;
        notification.config = config;
        events::EventDispatcher::instance().publish(notification);

        // Publish FFT config changed so renderer picks it up
        events::ocean::OceanFFTConfigChangedNotification fftNotif;
        fftNotif.config = oceanConfig;
        events::EventDispatcher::instance().publish(fftNotif);

        vfLogInfo("Created ocean entity");

        return oceanEntity;
    }

    bool OceanService::deleteOcean(EntityHandle entity)
    {
        if (!entity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return false;

        scene::Entity oceanEnt(ent);
        sceneGraph->removeEntity(oceanEnt);

        oceanEntity = {};
        oceanConfig = OceanFFTConfigData{};
        oceanConfigVersion++;

        events::ocean::OceanDeletedNotification notification;
        notification.oceanEntity = entity;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    std::optional<OceanData> OceanService::getOceanData(EntityHandle entity) const
    {
        if (!entity.isValid())
            return std::nullopt;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return std::nullopt;

        const auto& comp = registry.get<components::OceanComponent>(ent);

        OceanData data;
        data.waterHeight = comp.waterHeight;
        data.physicsEnabled = comp.physicsEnabled;
        data.isActive = comp.isActive;
        data.shallowColor = comp.shallowColor;
        data.deepColor = comp.deepColor;
        data.maxVisibleDepth = comp.maxVisibleDepth;
        data.fresnelPower = comp.fresnelPower;
        data.refractionStrength = comp.refractionStrength;
        data.refractionChromatic = comp.refractionChromatic;
        data.refractionDepthScale = comp.refractionDepthScale;
        data.causticStrength = comp.causticStrength;
        data.causticDepthFalloff = comp.causticDepthFalloff;
        data.shoreFoamRange = comp.shoreFoamRange;
        data.shoreFoamIntensity = comp.shoreFoamIntensity;
        data.shoreBreakingStrength = comp.shoreBreakingStrength;
        data.shoreWetRange = comp.shoreWetRange;
        data.shoreWetDarkening = comp.shoreWetDarkening;
        data.shoreWetRoughness = comp.shoreWetRoughness;
        // VK-1604
        data.ssrEnabled = comp.ssrEnabled;
        data.ssrIntensity = comp.ssrIntensity;
        data.ssrMaxDistance = comp.ssrMaxDistance;
        data.ssrThickness = comp.ssrThickness;
        data.ssrMaxSteps = comp.ssrMaxSteps;
        data.ssrDebugView = comp.ssrDebugView;
        data.beerLambertEnabled = comp.beerLambertEnabled;
        data.absorptionCoeff = comp.absorptionCoeff;
        data.scatteringColor = comp.scatteringColor;
        data.scatterCoeff = comp.scatterCoeff;
        data.absorptionMaxDistance = comp.absorptionMaxDistance;
        data.hexTilingEnabled = comp.hexTilingEnabled;
        data.hexBandMask = comp.hexBandMask;
        data.hexCellScale = comp.hexCellScale;
        data.hexBlendContrast = comp.hexBlendContrast;
        // VK-1605
        data.shoalingEnabled = comp.shoalingEnabled;
        data.shoalingStrength = comp.shoalingStrength;
        data.shoalingMinDepth = comp.shoalingMinDepth;
        data.shoalingWavelengthScale = comp.shoalingWavelengthScale;
        data.shoalingGamma = comp.shoalingGamma;
        data.shoreEdgeFadeStart = comp.shoreEdgeFadeStart;
        data.shoreWavesEnabled = comp.shoreWavesEnabled;
        data.shoreWaveAmplitude = comp.shoreWaveAmplitude;
        data.shoreWaveLength = comp.shoreWaveLength;
        data.shoreWaveSpeed = comp.shoreWaveSpeed;
        data.shoreWaveBreakDepth = comp.shoreWaveBreakDepth;
        data.shoreWaveBreakRange = comp.shoreWaveBreakRange;
        data.shoreWaveCrestFoam = comp.shoreWaveCrestFoam;
        data.shoreWaveCrestFoamThreshold = comp.shoreWaveCrestFoamThreshold;
        data.shoreWaveLean = comp.shoreWaveLean;
        // VK-1606
        data.rippleSimEnabled = comp.rippleSimEnabled;
        data.ripplePatchSize = comp.ripplePatchSize;
        data.rippleWaveSpeed = comp.rippleWaveSpeed;
        data.rippleDamping = comp.rippleDamping;
        data.rippleHeightScale = comp.rippleHeightScale;
        data.rippleNormalScale = comp.rippleNormalScale;
        data.rippleFoamGain = comp.rippleFoamGain;
        data.rippleFoamScale = comp.rippleFoamScale;
        data.rippleFoamDecay = comp.rippleFoamDecay;
        data.rippleEdgeFadeStart = comp.rippleEdgeFadeStart;
        data.weatherDriven = comp.weatherDriven;
        data.weatherResponse = comp.weatherResponse;
        data.currentBeaufort = comp.currentBeaufort;
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            data.oceanConfig.bands[i].resolution = comp.oceanBands[i].resolution;
            data.oceanConfig.bands[i].patchSize = comp.oceanBands[i].patchSize;
            data.oceanConfig.bands[i].windSpeed = comp.oceanBands[i].windSpeed;
            data.oceanConfig.bands[i].windDirection = comp.oceanBands[i].windDirection;
            data.oceanConfig.bands[i].amplitude = comp.oceanBands[i].amplitude;
            data.oceanConfig.bands[i].choppiness = comp.oceanBands[i].choppiness;
            data.oceanConfig.bands[i].foamThreshold = comp.oceanBands[i].foamThreshold;
            data.oceanConfig.bands[i].displacementScale = comp.oceanBands[i].displacementScale;
            data.oceanConfig.bands[i].enabled = comp.oceanBands[i].enabled;
            data.oceanConfig.bands[i].foamPersistence = comp.oceanBands[i].foamPersistence;
            data.oceanConfig.bands[i].foamDecay = comp.oceanBands[i].foamDecay;
        }
        data.oceanConfig.gravity = comp.oceanGravity;
        data.oceanConfig.enabled = comp.isActive;

        return data;
    }

    bool OceanService::hasOceanComponent(EntityHandle entity) const
    {
        if (!entity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);

        if (!registry.valid(ent))
            return false;

        return registry.all_of<components::OceanComponent>(ent);
    }

    bool OceanService::isPositionInOcean(const glm::vec3& worldPos) const
    {
        // VK-1607: a water body counts as water even with no ocean in the scene, so the old
        // "no ocean entity -> not in water" early-out cannot stand on its own any more. Bodies are
        // bounded, which is why this resolves through resolveSurfaceHeight rather than
        // getOceanHeightAt: outside every body AND with no ocean there is simply no surface, and
        // that is different from a surface at y = 0.
        const glm::vec2 worldXZ(worldPos.x, worldPos.z);
        const std::vector<water::WaterBodyDesc> bodies = collectWaterBodies();

        // Bodies first, so the ocean height sampler (which sums every FFT band) is only paid for
        // when the point is actually over open water.
        const int bodyIndex = water::findBodyAt(bodies.data(), bodies.size(), worldXZ);
        if (bodyIndex >= 0)
            return worldPos.y <= bodies[static_cast<size_t>(bodyIndex)].surfaceHeight;

        if (!oceanEntity.isValid())
            return false;

        return worldPos.y <= getOceanSurfaceHeightAt(worldXZ);
    }

    // ------------------------------------------------------------------------------------------
    // VK-1605: shore depth field
    // ------------------------------------------------------------------------------------------

    void OceanService::beginShoreFieldRebake(const glm::vec2& cameraXZ)
    {
        // One snapshot per rebake, never one query per sample: GetTerrainHeightAtQuery linearly
        // scans every tile per call, so the 65 536 samples a bake needs would be O(samples x tiles).
        // The snapshot is a full copy of the loaded height data, which is why it is released again
        // as soon as the bake completes.
        terrainSnapshot = {};
        terrainGrid = {};

        try
        {
            terrainSnapshot = ::events::EventDispatcher::instance().query(
                ::events::terrain::GetTerrainHeightfieldQuery{});
        }
        catch (...)
        {
            // No terrain service registered (e.g. a runtime build without terrain) — bake a field
            // of "no bottom", which makes every shoreline factor exactly 1.
        }

        if (terrainSnapshot.valid && !terrainSnapshot.heights.empty())
        {
            terrainGrid.worldOriginX = terrainSnapshot.worldOriginX;
            terrainGrid.worldOriginZ = terrainSnapshot.worldOriginZ;
            terrainGrid.tileWorldSize = terrainSnapshot.tileWorldSize;
            terrainGrid.vertexSpacing = terrainSnapshot.vertexSpacing;
            terrainGrid.gridCountX = terrainSnapshot.gridCountX;
            terrainGrid.gridCountZ = terrainSnapshot.gridCountZ;
            terrainGrid.verticesPerTile = terrainSnapshot.verticesPerTile;
            terrainGrid.heights = terrainSnapshot.heights.data();
            terrainGrid.heightCount = terrainSnapshot.heights.size();
            terrainGrid.tileValid = terrainSnapshot.tileValid.empty()
                                        ? nullptr : terrainSnapshot.tileValid.data();
            terrainGrid.tileValidCount = terrainSnapshot.tileValid.size();
        }
        shoreFieldHadTerrain = terrainGrid.isValid();

        const float waterHeight = getBaseWaterHeight();
        shoreFieldWaterHeight = waterHeight;
        const water::TerrainHeightGrid* grid = &terrainGrid;

        shoreDepthField.beginRebake(cameraXZ, waterHeight,
            [grid](float worldX, float worldZ, float& outHeight)
            {
                return grid->sample(worldX, worldZ, outHeight);
            });

        shoreFieldRebakeRequested = false;
    }

    void OceanService::updateShoreDepthField(const glm::vec2& cameraXZ)
    {
        if (!oceanEntity.isValid())
            return;

        // The field stores waterHeight - terrainHeight, so raising or lowering the ocean
        // invalidates every texel just as surely as moving the window does.
        const bool waterHeightChanged =
            std::abs(getBaseWaterHeight() - shoreFieldWaterHeight) > 0.001f;

        if (!shoreDepthField.isBaking() &&
            (shoreFieldRebakeRequested || waterHeightChanged || shoreDepthField.needsRebake(cameraXZ)))
        {
            beginShoreFieldRebake(cameraXZ);
        }

        if (shoreDepthField.bakeRows(water::SHORE_FIELD_ROWS_PER_TICK))
        {
            // Bake committed — the snapshot has done its job and can go. It is the largest
            // transient allocation in this path, so hold it no longer than necessary.
            terrainGrid = {};
            terrainSnapshot = {};
        }
    }

    float OceanService::getWaterDepthAt(const glm::vec2& worldXZ) const
    {
        return shoreDepthField.sample(worldXZ);
    }

    ShoreDepthFieldStatus OceanService::getShoreDepthFieldStatus() const
    {
        ShoreDepthFieldStatus status;
        status.hasTerrain = shoreFieldHadTerrain;
        status.baked = shoreDepthField.hasBakedOnce();
        status.baking = shoreDepthField.isBaking();
        status.progress = shoreDepthField.bakeProgress();
        status.version = shoreDepthField.version();
        status.center = shoreDepthField.center();
        status.windowSize = shoreDepthField.windowSize();
        status.resolution = shoreDepthField.resolution();
        return status;
    }

    void OceanService::queueWaterImpulse(const water::WaterImpulse& impulse)
    {
        if (impulse.radius <= 0.0f || impulse.strength == 0.0f)
            return;

        std::lock_guard<std::mutex> lock(impulseMutex);

        // Hard cap so a runaway script cannot grow this without bound between two drains. The sim
        // only consumes MAX_WATER_IMPULSES per step anyway; keep the newest.
        constexpr std::size_t QUEUE_CAP = 4 * water::MAX_WATER_IMPULSES;
        if (pendingImpulses.size() >= QUEUE_CAP)
            pendingImpulses.erase(pendingImpulses.begin());

        pendingImpulses.push_back(impulse);
    }

    std::vector<water::WaterImpulse> OceanService::drainWaterImpulses()
    {
        std::vector<water::WaterImpulse> drained;
        {
            std::lock_guard<std::mutex> lock(impulseMutex);
            pendingImpulses.swap(drained);
        }
        return drained;
    }

    void OceanService::setRippleSimEnabled(bool enabled)
    {
        if (!oceanEntity.isValid())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return;

        registry.get<components::OceanComponent>(ent).rippleSimEnabled = enabled;
    }

    bool OceanService::isRippleSimEnabled() const
    {
        if (!oceanEntity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return false;

        return registry.get<components::OceanComponent>(ent).rippleSimEnabled;
    }

    void OceanService::updateWakeEmitters(float deltaTime)
    {
        if (deltaTime <= 0.0f || !isRippleSimEnabled())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WaterWakeEmitterComponent, components::TransformComponent>();

        const float surfaceBase = getBaseWaterHeight();

        for (auto entity : view)
        {
            const auto& emitter = view.get<components::WaterWakeEmitterComponent>(entity);
            if (!emitter.enabled || emitter.radius <= 0.0f || emitter.strength == 0.0f)
                continue;

            const auto& transform = view.get<components::TransformComponent>(entity);
            const glm::vec3 world = transform.position + emitter.offset;
            const glm::vec2 worldXZ(world.x, world.z);

            EntityHandle handle = internal::toHandle(entity);

            // Speed from the change in position rather than from a rigid body, so scripted movers,
            // navmesh agents and character controllers all emit without needing physics.
            auto it = emitterWakeTrail.find(handle);
            if (it == emitterWakeTrail.end())
            {
                emitterWakeTrail.emplace(handle, worldXZ);
                continue;   // no previous sample yet, so no speed yet
            }

            const glm::vec2 previous = it->second;
            const float travelled = glm::length(worldXZ - previous);
            const float speed = travelled / deltaTime;

            if (speed < emitter.minSpeed)
                continue;

            // Only emit above the waterline-ish: an emitter dragged along under the seabed or high
            // in the air should not stamp rings on the surface.
            if (world.y > surfaceBase + emitter.radius || world.y < surfaceBase - emitter.radius)
                continue;

            const bool travelledEnough = travelled >= emitter.travelInterval;
            if (!emitter.continuous && !travelledEnough)
                continue;

            queueWaterImpulse({worldXZ, emitter.radius, emitter.strength});
            it->second = worldXZ;
        }

        // Bound the map the same way ControllerServiceImpl bounds its interpolation cache: any key
        // that is no longer a valid entity belongs to something destroyed.
        for (auto it = emitterWakeTrail.begin(); it != emitterWakeTrail.end();)
        {
            if (registry.valid(internal::fromHandle(it->first)))
                ++it;
            else
                it = emitterWakeTrail.erase(it);
        }
    }

    float OceanService::getOceanHeightAt(const glm::vec2& worldXZ) const
    {
        // VK-1607: water bodies win over the ocean wherever they cover the point. This is the single
        // choke point every consumer already goes through - GetOceanHeightAtQuery, the
        // Ocean.getOceanHeightAt script native, and updateBuoyancy's per-sample-point call - so
        // making it body-aware makes all of them body-aware with no API change.
        //
        // A body is flat by definition, so there is no FFT displacement to add on top.
        const std::vector<water::WaterBodyDesc> bodies = collectWaterBodies();
        const int bodyIndex = water::findBodyAt(bodies.data(), bodies.size(), worldXZ);
        if (bodyIndex >= 0)
            return bodies[static_cast<size_t>(bodyIndex)].surfaceHeight;

        return getOceanSurfaceHeightAt(worldXZ);
    }

    std::optional<float> OceanService::getWaterSurfaceAt(const glm::vec2& worldXZ) const
    {
        const std::vector<water::WaterBodyDesc> bodies = collectWaterBodies();
        const int bodyIndex = water::findBodyAt(bodies.data(), bodies.size(), worldXZ);
        if (bodyIndex >= 0)
            return bodies[static_cast<size_t>(bodyIndex)].surfaceHeight;

        if (!oceanEntity.isValid())
            return std::nullopt;

        return getOceanSurfaceHeightAt(worldXZ);
    }

    float OceanService::getOceanSurfaceHeightAt(const glm::vec2& worldXZ) const
    {
        if (!oceanEntity.isValid())
            return 0.0f;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return 0.0f;

        const auto& comp = registry.get<components::OceanComponent>(ent);
        float baseHeight = comp.waterHeight;

        if (oceanConfig.enabled && oceanHeightSampler)
            baseHeight += oceanHeightSampler(worldXZ);

        return baseHeight;
    }

    // ------------------------------------------------------------------------------------------
    // VK-1607: water bodies
    // ------------------------------------------------------------------------------------------

    bool OceanService::hasWaterToRender() const
    {
        if (hasActiveOcean())
            return true;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WaterBodyComponent>();
        for (auto entity : view)
        {
            if (view.get<components::WaterBodyComponent>(entity).isActive)
                return true;
        }
        return false;
    }

    std::vector<water::WaterBodyDesc> OceanService::collectWaterBodies() const
    {
        std::vector<water::WaterBodyDesc> bodies;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WaterBodyComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& body = view.get<components::WaterBodyComponent>(entity);
            if (!body.isActive)
                continue;

            // transform.position rather than a resolved world matrix, matching how VK-1606's
            // updateWakeEmitters reads emitter positions. Bodies are authored at the scene root, so
            // the two agree; parenting a body under a moving entity is out of scope for this
            // milestone.
            const auto& transform = view.get<components::TransformComponent>(entity);

            water::WaterBodyDesc desc;
            desc.center = glm::vec2(transform.position.x, transform.position.z);
            desc.halfExtents = glm::max(body.halfExtents, glm::vec2(0.0f));
            desc.surfaceHeight = transform.position.y + body.waterHeight;
            desc.bandMask = body.bandMask & water::WATER_TILE_BAND_MASK_BITS;
            desc.physicsEnabled = body.physicsEnabled ? 1u : 0u;
            bodies.push_back(desc);
        }

        return bodies;
    }

    EntityHandle OceanService::createWaterBody(const WaterBodyComponentData& data,
                                               const glm::vec3& position, const std::string& name)
    {
        scene::Entity entity(name.empty() ? std::string("WaterBody") : name);
        sceneGraph->addChild(sceneGraph->GetRoot(), entity);

        entity.getComponent<components::TransformComponent>().position = position;

        auto& comp = entity.addComponent<components::WaterBodyComponent>();
        applyWaterBodyData(comp, data);

        vfLogInfo("Created water body entity");
        return internal::toHandle(entity.getHandle());
    }

    void OceanService::applyWaterBodyData(components::WaterBodyComponent& comp,
                                          const WaterBodyComponentData& data)
    {
        comp.type = data.type == 1u ? components::WaterBodyType::Pool : components::WaterBodyType::Lake;
        comp.waterHeight = data.waterHeight;
        comp.halfExtents = glm::max(data.halfExtents, glm::vec2(0.0f));
        comp.bandMask = data.bandMask & water::WATER_TILE_BAND_MASK_BITS;
        comp.physicsEnabled = data.physicsEnabled;
        comp.isActive = data.isActive;
    }

    WaterBodyComponentData OceanService::waterBodyDataFromComponent(const components::WaterBodyComponent& comp)
    {
        WaterBodyComponentData data;
        data.type = static_cast<uint32_t>(comp.type);
        data.waterHeight = comp.waterHeight;
        data.halfExtents = comp.halfExtents;
        data.bandMask = comp.bandMask;
        data.physicsEnabled = comp.physicsEnabled;
        data.isActive = comp.isActive;
        return data;
    }

    void OceanService::addWaterBodyComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);
        if (!registry.valid(ent) || registry.all_of<components::WaterBodyComponent>(ent))
            return;

        registry.emplace<components::WaterBodyComponent>(ent);
    }

    void OceanService::removeWaterBodyComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);
        if (!registry.valid(ent) || !registry.all_of<components::WaterBodyComponent>(ent))
            return;

        registry.remove<components::WaterBodyComponent>(ent);
    }

    void OceanService::setWaterBodyData(EntityHandle entity, const WaterBodyComponentData& data)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);
        if (!registry.valid(ent) || !registry.all_of<components::WaterBodyComponent>(ent))
            return;

        applyWaterBodyData(registry.get<components::WaterBodyComponent>(ent), data);
    }

    std::optional<WaterBodyComponentData> OceanService::getWaterBodyData(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);
        if (!registry.valid(ent) || !registry.all_of<components::WaterBodyComponent>(ent))
            return std::nullopt;

        return waterBodyDataFromComponent(registry.get<components::WaterBodyComponent>(ent));
    }

    bool OceanService::hasWaterBodyComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);
        return registry.valid(ent) && registry.all_of<components::WaterBodyComponent>(ent);
    }

    std::vector<WaterBodyEntry> OceanService::getWaterBodies() const
    {
        std::vector<WaterBodyEntry> entries;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WaterBodyComponent>();
        for (auto entity : view)
        {
            WaterBodyEntry entry;
            entry.entity = internal::toHandle(entity);
            entry.data = waterBodyDataFromComponent(view.get<components::WaterBodyComponent>(entity));
            entries.push_back(entry);
        }

        return entries;
    }

    void OceanService::registerWaterBodyHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::ocean::CreateWaterBodyCommand>(
            [this](const events::ocean::CreateWaterBodyCommand& cmd)
            {
                return createWaterBody(cmd.data, cmd.position, cmd.name);
            });

        dispatcher.registerCommandHandler<events::ocean::AddWaterBodyComponentCommand>(
            [this](const events::ocean::AddWaterBodyComponentCommand& cmd)
            {
                addWaterBodyComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ocean::RemoveWaterBodyComponentCommand>(
            [this](const events::ocean::RemoveWaterBodyComponentCommand& cmd)
            {
                removeWaterBodyComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ocean::SetWaterBodyDataCommand>(
            [this](const events::ocean::SetWaterBodyDataCommand& cmd)
            {
                setWaterBodyData(cmd.entity, cmd.data);
            });

        dispatcher.registerQueryHandler<events::ocean::GetWaterBodyDataQuery>(
            [this](const events::ocean::GetWaterBodyDataQuery& query)
            {
                return getWaterBodyData(query.entity);
            });

        dispatcher.registerQueryHandler<events::ocean::HasWaterBodyComponentQuery>(
            [this](const events::ocean::HasWaterBodyComponentQuery& query)
            {
                return hasWaterBodyComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ocean::GetWaterBodiesQuery>(
            [this](const events::ocean::GetWaterBodiesQuery&)
            {
                return getWaterBodies();
            });

        dispatcher.registerQueryHandler<events::ocean::HasAnyWaterQuery>(
            [this](const events::ocean::HasAnyWaterQuery&)
            {
                return hasWaterToRender();
            });

        dispatcher.registerQueryHandler<events::ocean::GetWaterSurfaceAtQuery>(
            [this](const events::ocean::GetWaterSurfaceAtQuery& query)
            {
                return getWaterSurfaceAt(query.worldXZ);
            });
    }

    OceanVisualSettings OceanService::getOceanVisualSettings() const
    {
        OceanVisualSettings settings;
        if (!oceanEntity.isValid())
            return settings;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return settings;

        return visualSettingsFromComponent(registry.get<components::OceanComponent>(ent));
    }

    std::optional<OceanVisualSettings> OceanService::getOceanVisualSettings(EntityHandle entity) const
    {
        if (!entity.isValid())
            return std::nullopt;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return std::nullopt;

        return visualSettingsFromComponent(registry.get<components::OceanComponent>(ent));
    }

    OceanVisualSettings OceanService::visualSettingsFromComponent(const components::OceanComponent& comp)
    {
        OceanVisualSettings settings;
        settings.shallowColor = comp.shallowColor;
        settings.deepColor = comp.deepColor;
        settings.maxVisibleDepth = comp.maxVisibleDepth;
        settings.fresnelPower = comp.fresnelPower;
        settings.refractionStrength = comp.refractionStrength;
        settings.refractionChromatic = comp.refractionChromatic;
        settings.refractionDepthScale = comp.refractionDepthScale;
        settings.causticStrength = comp.causticStrength;
        settings.causticDepthFalloff = comp.causticDepthFalloff;
        settings.shoreFoamRange = comp.shoreFoamRange;
        settings.shoreFoamIntensity = comp.shoreFoamIntensity;
        settings.shoreBreakingStrength = comp.shoreBreakingStrength;
        settings.shoreWetRange = comp.shoreWetRange;
        settings.shoreWetDarkening = comp.shoreWetDarkening;
        settings.shoreWetRoughness = comp.shoreWetRoughness;
        // VK-1604
        settings.ssrEnabled = comp.ssrEnabled;
        settings.ssrIntensity = comp.ssrIntensity;
        settings.ssrMaxDistance = comp.ssrMaxDistance;
        settings.ssrThickness = comp.ssrThickness;
        settings.ssrMaxSteps = comp.ssrMaxSteps;
        settings.ssrDebugView = comp.ssrDebugView;
        settings.beerLambertEnabled = comp.beerLambertEnabled;
        settings.absorptionCoeff = comp.absorptionCoeff;
        settings.scatteringColor = comp.scatteringColor;
        settings.scatterCoeff = comp.scatterCoeff;
        settings.absorptionMaxDistance = comp.absorptionMaxDistance;
        settings.hexTilingEnabled = comp.hexTilingEnabled;
        settings.hexBandMask = comp.hexBandMask;
        settings.hexCellScale = comp.hexCellScale;
        settings.hexBlendContrast = comp.hexBlendContrast;
        // VK-1605
        settings.shoalingEnabled = comp.shoalingEnabled;
        settings.shoalingStrength = comp.shoalingStrength;
        settings.shoalingMinDepth = comp.shoalingMinDepth;
        settings.shoalingWavelengthScale = comp.shoalingWavelengthScale;
        settings.shoalingGamma = comp.shoalingGamma;
        settings.shoreEdgeFadeStart = comp.shoreEdgeFadeStart;
        settings.shoreWavesEnabled = comp.shoreWavesEnabled;
        settings.shoreWaveAmplitude = comp.shoreWaveAmplitude;
        settings.shoreWaveLength = comp.shoreWaveLength;
        settings.shoreWaveSpeed = comp.shoreWaveSpeed;
        settings.shoreWaveBreakDepth = comp.shoreWaveBreakDepth;
        settings.shoreWaveBreakRange = comp.shoreWaveBreakRange;
        settings.shoreWaveCrestFoam = comp.shoreWaveCrestFoam;
        settings.shoreWaveCrestFoamThreshold = comp.shoreWaveCrestFoamThreshold;
        settings.shoreWaveLean = comp.shoreWaveLean;
        // VK-1606
        settings.rippleSimEnabled = comp.rippleSimEnabled;
        settings.ripplePatchSize = comp.ripplePatchSize;
        settings.rippleWaveSpeed = comp.rippleWaveSpeed;
        settings.rippleDamping = comp.rippleDamping;
        settings.rippleHeightScale = comp.rippleHeightScale;
        settings.rippleNormalScale = comp.rippleNormalScale;
        settings.rippleFoamGain = comp.rippleFoamGain;
        settings.rippleFoamScale = comp.rippleFoamScale;
        settings.rippleFoamDecay = comp.rippleFoamDecay;
        settings.rippleEdgeFadeStart = comp.rippleEdgeFadeStart;

        return settings;
    }

    float OceanService::getBaseWaterHeight() const
    {
        if (!oceanEntity.isValid())
            return 0.0f;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return 0.0f;

        return registry.get<components::OceanComponent>(ent).waterHeight;
    }

    bool OceanService::saveOcean(EntityHandle entity, const std::string& path)
    {
        if (!entity.isValid())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(entity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return false;

        const auto& comp = registry.get<components::OceanComponent>(ent);

        ocean::OceanFileData fileData;
        fileData.waterHeight = comp.waterHeight;
        fileData.physicsEnabled = comp.physicsEnabled;

        fileData.shallowColor = comp.shallowColor;
        fileData.deepColor = comp.deepColor;
        fileData.maxVisibleDepth = comp.maxVisibleDepth;
        fileData.fresnelPower = comp.fresnelPower;
        fileData.refractionStrength = comp.refractionStrength;
        fileData.refractionChromatic = comp.refractionChromatic;
        fileData.refractionDepthScale = comp.refractionDepthScale;
        fileData.causticStrength = comp.causticStrength;
        fileData.causticDepthFalloff = comp.causticDepthFalloff;
        fileData.shoreFoamRange = comp.shoreFoamRange;
        fileData.shoreFoamIntensity = comp.shoreFoamIntensity;
        fileData.shoreBreakingStrength = comp.shoreBreakingStrength;
        fileData.shoreWetRange = comp.shoreWetRange;
        fileData.shoreWetDarkening = comp.shoreWetDarkening;
        fileData.shoreWetRoughness = comp.shoreWetRoughness;
        // VK-1604
        fileData.ssrEnabled = comp.ssrEnabled;
        fileData.ssrIntensity = comp.ssrIntensity;
        fileData.ssrMaxDistance = comp.ssrMaxDistance;
        fileData.ssrThickness = comp.ssrThickness;
        fileData.ssrMaxSteps = comp.ssrMaxSteps;
        fileData.ssrDebugView = comp.ssrDebugView;
        fileData.beerLambertEnabled = comp.beerLambertEnabled;
        fileData.absorptionCoeff = comp.absorptionCoeff;
        fileData.scatteringColor = comp.scatteringColor;
        fileData.scatterCoeff = comp.scatterCoeff;
        fileData.absorptionMaxDistance = comp.absorptionMaxDistance;
        fileData.hexTilingEnabled = comp.hexTilingEnabled;
        fileData.hexBandMask = comp.hexBandMask;
        fileData.hexCellScale = comp.hexCellScale;
        fileData.hexBlendContrast = comp.hexBlendContrast;
        // VK-1605
        fileData.shoalingEnabled = comp.shoalingEnabled;
        fileData.shoalingStrength = comp.shoalingStrength;
        fileData.shoalingMinDepth = comp.shoalingMinDepth;
        fileData.shoalingWavelengthScale = comp.shoalingWavelengthScale;
        fileData.shoalingGamma = comp.shoalingGamma;
        fileData.shoreEdgeFadeStart = comp.shoreEdgeFadeStart;
        fileData.shoreWavesEnabled = comp.shoreWavesEnabled;
        fileData.shoreWaveAmplitude = comp.shoreWaveAmplitude;
        fileData.shoreWaveLength = comp.shoreWaveLength;
        fileData.shoreWaveSpeed = comp.shoreWaveSpeed;
        fileData.shoreWaveBreakDepth = comp.shoreWaveBreakDepth;
        fileData.shoreWaveBreakRange = comp.shoreWaveBreakRange;
        fileData.shoreWaveCrestFoam = comp.shoreWaveCrestFoam;
        fileData.shoreWaveCrestFoamThreshold = comp.shoreWaveCrestFoamThreshold;
        fileData.shoreWaveLean = comp.shoreWaveLean;
        // VK-1606
        fileData.rippleSimEnabled = comp.rippleSimEnabled;
        fileData.ripplePatchSize = comp.ripplePatchSize;
        fileData.rippleWaveSpeed = comp.rippleWaveSpeed;
        fileData.rippleDamping = comp.rippleDamping;
        fileData.rippleHeightScale = comp.rippleHeightScale;
        fileData.rippleNormalScale = comp.rippleNormalScale;
        fileData.rippleFoamGain = comp.rippleFoamGain;
        fileData.rippleFoamScale = comp.rippleFoamScale;
        fileData.rippleFoamDecay = comp.rippleFoamDecay;
        fileData.rippleEdgeFadeStart = comp.rippleEdgeFadeStart;

        fileData.density = comp.density;
        fileData.drag = comp.drag;
        fileData.buoyancyStrength = comp.buoyancyStrength;

        fileData.weatherDriven = comp.weatherDriven;
        fileData.weatherResponse = comp.weatherResponse;
        fileData.currentBeaufort = comp.currentBeaufort;

        for (uint32_t i = 0; i < ocean::OceanFileData::MAX_BANDS; ++i)
        {
            fileData.bands[i].resolution = comp.oceanBands[i].resolution;
            fileData.bands[i].patchSize = comp.oceanBands[i].patchSize;
            fileData.bands[i].windSpeed = comp.oceanBands[i].windSpeed;
            fileData.bands[i].windDirection = comp.oceanBands[i].windDirection;
            fileData.bands[i].amplitude = comp.oceanBands[i].amplitude;
            fileData.bands[i].choppiness = comp.oceanBands[i].choppiness;
            fileData.bands[i].foamThreshold = comp.oceanBands[i].foamThreshold;
            fileData.bands[i].displacementScale = comp.oceanBands[i].displacementScale;
            fileData.bands[i].enabled = comp.oceanBands[i].enabled;
            fileData.bands[i].foamPersistence = comp.oceanBands[i].foamPersistence;
            fileData.bands[i].foamDecay = comp.oceanBands[i].foamDecay;
        }
        fileData.gravity = comp.oceanGravity;

        if (!ocean::OceanSerializer::save(path, fileData))
            return false;

        events::ocean::OceanSavedNotification notification;
        notification.oceanEntity = entity;
        notification.path = path;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    EntityHandle OceanService::loadOcean(const std::string& path)
    {
        ocean::OceanFileData fileData;
        if (!ocean::OceanSerializer::load(path, fileData))
            return {};

        // Delete existing ocean if any
        if (oceanEntity.isValid())
            deleteOcean(oceanEntity);

        // Create new ocean from loaded data
        OceanCreationData config;
        config.waterHeight = fileData.waterHeight;
        config.physicsEnabled = fileData.physicsEnabled;
        config.shallowColor = fileData.shallowColor;
        config.deepColor = fileData.deepColor;
        for (uint32_t i = 0; i < ocean::OceanFileData::MAX_BANDS; ++i)
        {
            config.oceanConfig.bands[i].resolution = fileData.bands[i].resolution;
            config.oceanConfig.bands[i].patchSize = fileData.bands[i].patchSize;
            config.oceanConfig.bands[i].windSpeed = fileData.bands[i].windSpeed;
            config.oceanConfig.bands[i].windDirection = fileData.bands[i].windDirection;
            config.oceanConfig.bands[i].amplitude = fileData.bands[i].amplitude;
            config.oceanConfig.bands[i].choppiness = fileData.bands[i].choppiness;
            config.oceanConfig.bands[i].foamThreshold = fileData.bands[i].foamThreshold;
            config.oceanConfig.bands[i].displacementScale = fileData.bands[i].displacementScale;
            config.oceanConfig.bands[i].enabled = fileData.bands[i].enabled;
            config.oceanConfig.bands[i].foamPersistence = fileData.bands[i].foamPersistence;
            config.oceanConfig.bands[i].foamDecay = fileData.bands[i].foamDecay;
        }
        config.oceanConfig.gravity = fileData.gravity;
        config.oceanConfig.enabled = true;

        EntityHandle handle = createOcean(config);
        if (!handle.isValid())
            return {};

        // Apply remaining settings
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(handle);
        if (registry.valid(ent) && registry.all_of<components::OceanComponent>(ent))
        {
            auto& comp = registry.get<components::OceanComponent>(ent);
            comp.maxVisibleDepth = fileData.maxVisibleDepth;
            comp.fresnelPower = fileData.fresnelPower;
            comp.refractionStrength = fileData.refractionStrength;
            comp.refractionChromatic = fileData.refractionChromatic;
            comp.refractionDepthScale = fileData.refractionDepthScale;
            comp.causticStrength = fileData.causticStrength;
            comp.causticDepthFalloff = fileData.causticDepthFalloff;
            comp.shoreFoamRange = fileData.shoreFoamRange;
            comp.shoreFoamIntensity = fileData.shoreFoamIntensity;
            comp.shoreBreakingStrength = fileData.shoreBreakingStrength;
            comp.shoreWetRange = fileData.shoreWetRange;
            comp.shoreWetDarkening = fileData.shoreWetDarkening;
            comp.shoreWetRoughness = fileData.shoreWetRoughness;
            // VK-1604
            comp.ssrEnabled = fileData.ssrEnabled;
            comp.ssrIntensity = fileData.ssrIntensity;
            comp.ssrMaxDistance = fileData.ssrMaxDistance;
            comp.ssrThickness = fileData.ssrThickness;
            comp.ssrMaxSteps = fileData.ssrMaxSteps;
            comp.ssrDebugView = fileData.ssrDebugView;
            comp.beerLambertEnabled = fileData.beerLambertEnabled;
            comp.absorptionCoeff = fileData.absorptionCoeff;
            comp.scatteringColor = fileData.scatteringColor;
            comp.scatterCoeff = fileData.scatterCoeff;
            comp.absorptionMaxDistance = fileData.absorptionMaxDistance;
            comp.hexTilingEnabled = fileData.hexTilingEnabled;
            comp.hexBandMask = fileData.hexBandMask;
            comp.hexCellScale = fileData.hexCellScale;
            comp.hexBlendContrast = fileData.hexBlendContrast;
            // VK-1605
            comp.shoalingEnabled = fileData.shoalingEnabled;
            comp.shoalingStrength = fileData.shoalingStrength;
            comp.shoalingMinDepth = fileData.shoalingMinDepth;
            comp.shoalingWavelengthScale = fileData.shoalingWavelengthScale;
            comp.shoalingGamma = fileData.shoalingGamma;
            comp.shoreEdgeFadeStart = fileData.shoreEdgeFadeStart;
            comp.shoreWavesEnabled = fileData.shoreWavesEnabled;
            comp.shoreWaveAmplitude = fileData.shoreWaveAmplitude;
            comp.shoreWaveLength = fileData.shoreWaveLength;
            comp.shoreWaveSpeed = fileData.shoreWaveSpeed;
            comp.shoreWaveBreakDepth = fileData.shoreWaveBreakDepth;
            comp.shoreWaveBreakRange = fileData.shoreWaveBreakRange;
            comp.shoreWaveCrestFoam = fileData.shoreWaveCrestFoam;
            comp.shoreWaveCrestFoamThreshold = fileData.shoreWaveCrestFoamThreshold;
            comp.shoreWaveLean = fileData.shoreWaveLean;
            // VK-1606
            comp.rippleSimEnabled = fileData.rippleSimEnabled;
            comp.ripplePatchSize = fileData.ripplePatchSize;
            comp.rippleWaveSpeed = fileData.rippleWaveSpeed;
            comp.rippleDamping = fileData.rippleDamping;
            comp.rippleHeightScale = fileData.rippleHeightScale;
            comp.rippleNormalScale = fileData.rippleNormalScale;
            comp.rippleFoamGain = fileData.rippleFoamGain;
            comp.rippleFoamScale = fileData.rippleFoamScale;
            comp.rippleFoamDecay = fileData.rippleFoamDecay;
            comp.rippleEdgeFadeStart = fileData.rippleEdgeFadeStart;
            comp.density = fileData.density;
            comp.drag = fileData.drag;
            comp.buoyancyStrength = fileData.buoyancyStrength;
            comp.weatherDriven = fileData.weatherDriven;
            comp.weatherResponse = fileData.weatherResponse;
            comp.currentBeaufort = fileData.currentBeaufort;
        }

        events::ocean::OceanLoadedNotification notification;
        notification.oceanEntity = handle;
        notification.path = path;
        events::EventDispatcher::instance().publish(notification);

        return handle;
    }

    void OceanService::updateBuoyancy()
    {
        if (!physicsProvider)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();

        // VK-1607: this used to bail whenever there was no ocean entity. A scene whose only water is
        // a lake still has to float boats, so the ocean is now one optional water source among
        // several. The tuning (density / drag / buoyancyStrength) still comes from the OceanComponent
        // when there is one, and falls back to its own defaults when there is not.
        static const components::OceanComponent defaultOceanTuning{};
        const components::OceanComponent* oceanComp = nullptr;
        if (oceanEntity.isValid())
        {
            entt::entity ent = internal::fromHandle(oceanEntity);
            if (registry.valid(ent) && registry.all_of<components::OceanComponent>(ent))
                oceanComp = &registry.get<components::OceanComponent>(ent);
        }

        // Ocean water only participates when its own physics switch is on; bodies carry their own.
        const bool oceanPhysicsActive = oceanComp != nullptr && oceanComp->physicsEnabled;
        const auto& comp = oceanComp ? *oceanComp : defaultOceanTuning;

        // Hoisted out of the per-body, per-sample-point loops below: one registry walk per physics
        // step instead of one per sample point.
        const std::vector<water::WaterBodyDesc> bodies = collectWaterBodies();

        if (!oceanPhysicsActive && bodies.empty())
            return;

        glm::vec3 gravity = physicsProvider->getGravity();
        float gravityMag = glm::length(gravity);

        // Check all dynamic rigid bodies for ocean submersion
        auto rbView = registry.view<components::RigidBodyComponent, components::TransformComponent>();
        for (auto rbEntity : rbView)
        {
            const auto& rb = rbView.get<components::RigidBodyComponent>(rbEntity);
            if (rb.type == components::RigidBodyType::Static)
                continue;

            EntityHandle handle = internal::toHandle(rbEntity);
            if (!physicsProvider->hasRigidBody(handle))
                continue;

            glm::vec3 pos = physicsProvider->getPosition(handle);
            glm::quat rot = physicsProvider->getRotation(handle);

            components::ColliderShape shape = components::ColliderShape::Box;
            glm::vec3 colliderSize{0.5f};
            float colliderHeight = 0.0f;
            glm::vec3 colliderOffset{0.0f};
            if (registry.all_of<components::ColliderComponent>(rbEntity))
            {
                const auto& collider = registry.get<components::ColliderComponent>(rbEntity);
                shape = collider.shape;
                colliderSize = collider.size;
                colliderHeight = collider.height;
                colliderOffset = collider.offset;
            }

            float halfHeight = water::colliderHalfHeight(shape, colliderSize, colliderHeight);

            // Hull sample points: explicit BuoyancyComponent points, or auto from the collider
            water::BuoyancySamplePoints samples;
            float buoyancyScale = 1.0f;
            float angularDrag = 0.0f;
            const auto* buoyancy = registry.try_get<components::BuoyancyComponent>(rbEntity);
            if (buoyancy)
            {
                buoyancyScale = buoyancy->buoyancyScale;
                angularDrag = buoyancy->angularDrag;
            }
            if (buoyancy && buoyancy->sampleMode == components::BuoyancyComponent::SampleMode::Custom &&
                buoyancy->customPointCount > 0)
            {
                samples.count = glm::min(buoyancy->customPointCount, water::MAX_BUOYANCY_POINTS);
                for (uint32_t i = 0; i < samples.count; ++i)
                    samples.points[i] = buoyancy->customPoints[i];
            }
            else
            {
                samples = water::generateSamplePoints(shape, colliderSize, colliderHeight, colliderOffset);
            }

            // Per-point submersion; the average drives drag and enter/exit tracking
            float totalSubmersion = 0.0f;
            float pointSubmersion[water::MAX_BUOYANCY_POINTS];
            glm::vec3 pointWorld[water::MAX_BUOYANCY_POINTS];
            for (uint32_t i = 0; i < samples.count; ++i)
            {
                pointWorld[i] = pos + rot * samples.points[i];
                const glm::vec2 pointXZ(pointWorld[i].x, pointWorld[i].z);

                // VK-1607: bodies first, ocean second - the same precedence getOceanHeightAt uses,
                // but resolved against the hoisted list and short-circuiting BEFORE the ocean height
                // sampler runs (that one sums every FFT band and is far from free). A body whose
                // physics switch is off is a hole in the water rather than a fall-through to the
                // ocean: something is occupying that footprint, it just does not float anything.
                const int bodyIndex = water::findBodyAt(bodies.data(), bodies.size(), pointXZ);

                float waterHeight;
                if (bodyIndex >= 0)
                {
                    const water::WaterBodyDesc& body = bodies[static_cast<size_t>(bodyIndex)];
                    if (body.physicsEnabled == 0u)
                    {
                        pointSubmersion[i] = 0.0f;
                        continue;
                    }
                    waterHeight = body.surfaceHeight;
                }
                else
                {
                    if (!oceanPhysicsActive)
                    {
                        pointSubmersion[i] = 0.0f;
                        continue;
                    }
                    waterHeight = getOceanSurfaceHeightAt(pointXZ);
                }

                pointSubmersion[i] = water::computeSubmersion(pointWorld[i].y, waterHeight, halfHeight);
                totalSubmersion += pointSubmersion[i];
            }
            float submersionRatio = samples.count > 0 ? totalSubmersion / static_cast<float>(samples.count) : 0.0f;

            // Track enter/exit for submersion notifications (published later on the main thread)
            bool wasInWater = entitiesInWater.contains(handle);
            bool isInWater = submersionRatio > 0.0f;

            if (isInWater && !wasInWater)
            {
                entitiesInWater.insert(handle);
                pendingWaterTransitions.push_back({handle, pos,
                                                   physicsProvider->getLinearVelocity(handle).y,
                                                   submersionRatio, true});
            }
            else if (!isInWater && wasInWater)
            {
                entitiesInWater.erase(handle);
                pendingWaterTransitions.push_back({handle, pos, 0.0f, 0.0f, false});
            }

            if (submersionRatio <= 0.0f)
                continue;

            float mass = rb.mass;

            // Per-point share keeps the net force identical to the old single-point version
            // when fully submerged, while differential submersion adds a righting torque.
            float forcePerPoint = mass * gravityMag * comp.buoyancyStrength * buoyancyScale /
                                  static_cast<float>(samples.count);
            for (uint32_t i = 0; i < samples.count; ++i)
            {
                if (pointSubmersion[i] <= 0.0f)
                    continue;
                physicsProvider->applyForceAtPosition(
                    handle, glm::vec3(0.0f, forcePerPoint * pointSubmersion[i], 0.0f), pointWorld[i]);
            }

            glm::vec3 velocity = physicsProvider->getLinearVelocity(handle);
            glm::vec3 dragForce = -velocity * comp.drag * submersionRatio * mass;
            physicsProvider->applyForce(handle, dragForce);

            if (angularDrag > 0.0f)
            {
                glm::vec3 angularVelocity = physicsProvider->getAngularVelocity(handle);
                physicsProvider->applyTorque(handle, -angularVelocity * angularDrag * submersionRatio * mass);
            }

            // VK-1606: automatic wake. Anything moving while partly submerged writes into the ripple
            // patch, so a boat leaves a trail with no authored emitter at all. Throttled by DISTANCE
            // rather than time, so the spacing between rings does not depend on how fast the hull is
            // going (a time throttle bunches them up at low speed and spreads them at high speed).
            if (comp.rippleSimEnabled)
            {
                constexpr float WAKE_MIN_SPEED = 0.75f;         // m/s
                constexpr float WAKE_TRAVEL_INTERVAL = 0.5f;    // metres between rings
                constexpr float WAKE_STRENGTH_SCALE = 0.06f;    // impulse per m/s of hull speed

                const float horizontalSpeed = glm::length(glm::vec2(velocity.x, velocity.z));
                if (horizontalSpeed >= WAKE_MIN_SPEED)
                {
                    const glm::vec2 posXZ(pos.x, pos.z);
                    auto [it, inserted] = buoyancyWakeTrail.try_emplace(handle, posXZ);
                    if (inserted || glm::distance(posXZ, it->second) >= WAKE_TRAVEL_INTERVAL)
                    {
                        it->second = posXZ;
                        // Negative: a hull pushes the surface DOWN and the water rebounds around it.
                        const float radius = glm::max(glm::max(colliderSize.x, colliderSize.z), 0.5f);
                        const float strength = -WAKE_STRENGTH_SCALE * horizontalSpeed * submersionRatio;
                        queueWaterImpulse({posXZ, radius, strength});
                    }
                }
            }
        }

        // Same bounded-growth rule as emitterWakeTrail; this map is owned by the physics worker.
        for (auto it = buoyancyWakeTrail.begin(); it != buoyancyWakeTrail.end();)
        {
            if (registry.valid(internal::fromHandle(it->first)))
                ++it;
            else
                it = buoyancyWakeTrail.erase(it);
        }
    }

    void OceanService::flushWaterEvents()
    {
        if (pendingWaterTransitions.empty())
            return;

        auto& dispatcher = events::EventDispatcher::instance();
        for (const auto& transition : pendingWaterTransitions)
        {
            if (transition.entered)
            {
                events::ocean::ObjectEnteredWaterNotification notification;
                notification.entity = transition.entity;
                notification.position = transition.position;
                notification.verticalSpeed = transition.verticalSpeed;
                notification.submersion = transition.submersion;
                dispatcher.publish(notification);
            }
            else
            {
                events::ocean::ObjectExitedWaterNotification notification;
                notification.entity = transition.entity;
                notification.position = transition.position;
                dispatcher.publish(notification);
            }
        }
        pendingWaterTransitions.clear();
    }

    void OceanService::clearBuoyancyTracking()
    {
        entitiesInWater.clear();
        pendingWaterTransitions.clear();

        // VK-1606: leaving play mode must not leave a stale wake trail behind — the next run would
        // measure a bogus "distance travelled" from wherever the entity was when play stopped.
        emitterWakeTrail.clear();
        buoyancyWakeTrail.clear();
        {
            std::lock_guard<std::mutex> lock(impulseMutex);
            pendingImpulses.clear();
        }
    }

    void OceanService::update(float deltaTime)
    {
        if (!oceanEntity.isValid())
            return;

        if (seaStateTransitionActive)
            updateManualSeaStateTransition(deltaTime);
        else
            updateWeatherDrivenSeaState();

        updateWakeEmitters(deltaTime);   // VK-1606
    }

    void OceanService::setSeaState(float beaufort, float transitionSeconds)
    {
        if (!oceanEntity.isValid())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return;

        auto& comp = registry.get<components::OceanComponent>(ent);
        float bf = glm::clamp(beaufort, 0.0f, 12.0f);
        // Keep the currently authored swell direction; weather-driven mode overrides it anyway
        float direction = comp.oceanBands[0].windDirection;
        water::SeaState target = water::seaStateFromBeaufort(bf, direction);

        if (transitionSeconds <= 0.0f)
        {
            seaStateTransitionActive = false;
            comp.currentBeaufort = bf;
            lastAppliedBeaufort = bf;
            applySeaState(target);
            return;
        }

        seaStateTransitionStart = seaStateFromComponentBands();
        seaStateTransitionTarget = target;
        seaStateStartBeaufort = comp.currentBeaufort;
        seaStateTargetBeaufort = bf;
        seaStateTransitionElapsed = 0.0f;
        seaStateTransitionDuration = transitionSeconds;
        seaStateTransitionActive = true;
    }

    float OceanService::getSeaState() const
    {
        if (!oceanEntity.isValid())
            return 0.0f;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return 0.0f;

        return registry.get<components::OceanComponent>(ent).currentBeaufort;
    }

    void OceanService::applySeaState(const water::SeaState& state)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return;

        auto& comp = registry.get<components::OceanComponent>(ent);
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS && i < water::SEA_STATE_BANDS; ++i)
        {
            // Only spectrum/push-constant parameters — never resolution or patchSize
            // (those tear down and rebuild the FFT GPU resources)
            comp.oceanBands[i].windSpeed = state.bands[i].windSpeed;
            comp.oceanBands[i].windDirection = state.bands[i].windDirection;
            comp.oceanBands[i].amplitude = state.bands[i].amplitude;
            comp.oceanBands[i].choppiness = state.bands[i].choppiness;
            comp.oceanBands[i].displacementScale = state.bands[i].displacementScale;
            comp.oceanBands[i].foamThreshold = state.bands[i].foamThreshold;

            oceanConfig.bands[i].windSpeed = state.bands[i].windSpeed;
            oceanConfig.bands[i].windDirection = state.bands[i].windDirection;
            oceanConfig.bands[i].amplitude = state.bands[i].amplitude;
            oceanConfig.bands[i].choppiness = state.bands[i].choppiness;
            oceanConfig.bands[i].displacementScale = state.bands[i].displacementScale;
            oceanConfig.bands[i].foamThreshold = state.bands[i].foamThreshold;
        }
        oceanConfigVersion++;
    }

    water::SeaState OceanService::seaStateFromComponentBands() const
    {
        water::SeaState state;
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return state;

        const auto& comp = registry.get<components::OceanComponent>(ent);
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS && i < water::SEA_STATE_BANDS; ++i)
        {
            state.bands[i].windSpeed = comp.oceanBands[i].windSpeed;
            state.bands[i].windDirection = comp.oceanBands[i].windDirection;
            state.bands[i].amplitude = comp.oceanBands[i].amplitude;
            state.bands[i].choppiness = comp.oceanBands[i].choppiness;
            state.bands[i].displacementScale = comp.oceanBands[i].displacementScale;
            state.bands[i].foamThreshold = comp.oceanBands[i].foamThreshold;
        }
        return state;
    }

    void OceanService::updateWeatherDrivenSeaState()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return;

        auto& comp = registry.get<components::OceanComponent>(ent);
        if (!comp.weatherDriven)
            return;

        weather::WeatherState ws;
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            if (!dispatcher.query(events::weather::IsWeatherEnabledQuery{}))
                return;
            ws = dispatcher.query(events::weather::GetWeatherStateQuery{});
        }
        catch (...)
        {
            // No weather service registered (e.g. a runtime build without weather) — keep
            // the authored sea state instead of failing every frame.
            return;
        }

        // Quantize so a slow weather transition doesn't bump the config version (and
        // re-dispatch the FFT spectrum) every single frame
        float bf = water::quantizeBeaufort(water::beaufortFromWeather(ws, comp.weatherResponse));
        float direction = water::quantizeDirectionDeg(ws.windDirectionDeg);
        if (bf == lastAppliedBeaufort && direction == lastAppliedWindDirection)
            return;

        lastAppliedBeaufort = bf;
        lastAppliedWindDirection = direction;
        comp.currentBeaufort = bf;
        applySeaState(water::seaStateFromBeaufort(bf, direction));
    }

    void OceanService::updateManualSeaStateTransition(float deltaTime)
    {
        seaStateTransitionElapsed += deltaTime;
        float t = seaStateTransitionDuration > 0.0f
                      ? glm::clamp(seaStateTransitionElapsed / seaStateTransitionDuration, 0.0f, 1.0f)
                      : 1.0f;

        applySeaState(water::lerpSeaState(seaStateTransitionStart, seaStateTransitionTarget, t));

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (registry.valid(ent) && registry.all_of<components::OceanComponent>(ent))
        {
            registry.get<components::OceanComponent>(ent).currentBeaufort =
                glm::mix(seaStateStartBeaufort, seaStateTargetBeaufort, t);
        }

        if (t >= 1.0f)
        {
            seaStateTransitionActive = false;
            // Let weather-driven mode (if on) take back over from the new state
            lastAppliedBeaufort = -1.0f;
            lastAppliedWindDirection = -10000.0f;
        }
    }

    void OceanService::rebuildOceanFromComponents()
    {
        oceanEntity = {};
        entitiesInWater.clear();
        pendingWaterTransitions.clear();
        seaStateTransitionActive = false;
        lastAppliedBeaufort = -1.0f;
        lastAppliedWindDirection = -10000.0f;
        // VK-1605: a new scene means a new bathymetry.
        shoreFieldRebakeRequested = true;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto oceanView = registry.view<components::OceanComponent>();

        for (auto entity : oceanView)
        {
            oceanEntity = internal::toHandle(entity);

            const auto& comp = registry.get<components::OceanComponent>(entity);
            for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
            {
                oceanConfig.bands[i].resolution = comp.oceanBands[i].resolution;
                oceanConfig.bands[i].patchSize = comp.oceanBands[i].patchSize;
                oceanConfig.bands[i].windSpeed = comp.oceanBands[i].windSpeed;
                oceanConfig.bands[i].windDirection = comp.oceanBands[i].windDirection;
                oceanConfig.bands[i].amplitude = comp.oceanBands[i].amplitude;
                oceanConfig.bands[i].choppiness = comp.oceanBands[i].choppiness;
                oceanConfig.bands[i].foamThreshold = comp.oceanBands[i].foamThreshold;
                oceanConfig.bands[i].displacementScale = comp.oceanBands[i].displacementScale;
                oceanConfig.bands[i].enabled = comp.oceanBands[i].enabled;
                oceanConfig.bands[i].foamPersistence = comp.oceanBands[i].foamPersistence;
                oceanConfig.bands[i].foamDecay = comp.oceanBands[i].foamDecay;
            }
            oceanConfig.gravity = comp.oceanGravity;
            oceanConfig.enabled = comp.isActive;
            oceanConfigVersion++;

            break; // Only one ocean entity supported
        }

        if (oceanEntity.isValid())
        {
            vfLogInfo("OceanService: Rebuilt ocean from components");
        }
    }

    void OceanService::onEntityDeleted(EntityHandle entity)
    {
        if (entity.isValid() && entity.id == oceanEntity.id)
        {
            oceanEntity = {};
            oceanConfig = OceanFFTConfigData{};
            oceanConfigVersion++;

            events::ocean::OceanDeletedNotification notification;
            notification.oceanEntity = entity;
            events::EventDispatcher::instance().publish(notification);
        }
    }

    void OceanService::onSceneCleared()
    {
        entitiesInWater.clear();
        oceanEntity = {};
        oceanConfig = OceanFFTConfigData{};
        oceanConfigVersion++;

        vfLogInfo("OceanService: Cleared ocean on scene clear");
    }
}
