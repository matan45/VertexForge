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

    std::vector<water::WaterTile*> WaterService::getVisibleWaterTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<water::WaterTile*> result;

        for (auto& [entityId, grid] : waterGrids)
        {
            // Run water streaming before visibility query
            auto streamerIt = waterStreamers.find(entityId);
            if (streamerIt != waterStreamers.end() && streamerIt->second && streamerIt->second->isEnabled())
            {
                auto defIt = definitionMaps.find(entityId);
                if (defIt != definitionMaps.end())
                {
                    EntityHandle waterHandle{entityId};
                    float tileSize = grid->getConfig().worldTileSize;

                    streamerIt->second->update(
                        cameraPosition, tileSize, defIt->second, *grid, waterStreamingActions);

                    bool streamingChanged = false;
                    for (const auto& action : waterStreamingActions)
                    {
                        if (action.isLoad)
                        {
                            // Guard: skip if tile already exists in grid (avoids duplicate entities)
                            if (grid->hasTile(action.coord))
                                continue;

                            const auto* def = defIt->second.getDefinition(action.coord);
                            if (def)
                            {
                                water::WaterTile* tile = grid->getOrCreateTile(action.coord);
                                if (tile)
                                {
                                    tile->updateHeight(def->waterHeight, tileSize);
                                    tile->waveIntensity = def->waveIntensity;
                                    tile->physicsEnabled = def->physicsEnabled;
                                    createTileEntity(waterHandle, tile, tileSize);
                                    streamingChanged = true;
                                }
                            }
                        }
                        else
                        {
                            // Remove tile entity and grid tile
                            scene::Entity parentEntity(internal::fromHandle(waterHandle));
                            for (auto& child : parentEntity.getChildren())
                            {
                                if (!child.hasComponent<components::WaterTileComponent>())
                                    continue;
                                const auto& tileComp = child.getComponent<components::WaterTileComponent>();
                                if (tileComp.tileX == action.coord.x && tileComp.tileZ == action.coord.z)
                                {
                                    EntityHandle tileHandle = internal::toHandle(child.getHandle());
                                    if (physicsProvider)
                                        physicsProvider->removeWaterSensorBody(tileHandle);
                                    sceneGraph->removeEntity(child);
                                    break;
                                }
                            }
                            grid->removeTile(action.coord);
                            streamingChanged = true;
                        }
                    }

                    // Sync component bounds and tile count after streaming changes
                    if (streamingChanged)
                    {
                        auto& registry = scene::EntityRegistry::getRegistry();
                        entt::entity ent = internal::fromHandle(waterHandle);
                        if (registry.valid(ent) && registry.all_of<components::WaterComponent>(ent))
                        {
                            auto& comp = registry.get<components::WaterComponent>(ent);
                            int32_t minX, minZ, maxX, maxZ;
                            grid->computeBounds(minX, minZ, maxX, maxZ);
                            comp.gridMinX = minX;
                            comp.gridMinZ = minZ;
                            comp.gridMaxX = maxX;
                            comp.gridMaxZ = maxZ;
                            comp.activeTileCount = static_cast<uint32_t>(grid->getTileCount());
                        }
                    }
                }
            }

            auto visibleTiles = grid->getVisibleTiles(frustum);

            if (distanceCullingEnabled && maxWaterDistSq > 0.0f)
            {
                for (auto* tile : visibleTiles)
                {
                    glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
                    glm::vec3 diff = tileCenter - cameraPosition;
                    float distSq = glm::dot(diff, diff);
                    if (distSq <= maxWaterDistSq)
                    {
                        result.push_back(tile);
                    }
                }
            }
            else
            {
                result.insert(result.end(), visibleTiles.begin(), visibleTiles.end());
            }

            // Update visibleTileCount on the component
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(EntityHandle{entityId});
            if (registry.valid(ent) && registry.all_of<components::WaterComponent>(ent))
            {
                registry.get<components::WaterComponent>(ent).visibleTileCount =
                    static_cast<uint32_t>(visibleTiles.size());
            }
        }

        return result;
    }

    std::vector<water::WaterTile*> WaterService::queryVisibleWaterTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<water::WaterTile*> result;

        for (auto& [entityId, grid] : waterGrids)
        {
            for (auto* tile : grid->getAllTiles())
            {
                if (!tile)
                    continue;

                if (!frustum.intersectsAABB(tile->worldBounds))
                    continue;

                if (distanceCullingEnabled && maxWaterDistSq > 0.0f)
                {
                    glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
                    glm::vec3 diff = tileCenter - cameraPosition;
                    float distSq = glm::dot(diff, diff);
                    if (distSq > maxWaterDistSq)
                        continue;
                }

                result.push_back(tile);
            }
        }

        return result;
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
