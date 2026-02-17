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
#include "../../events/TerrainEvents.hpp"
#include "../../providers/IPhysicsProvider.hpp"
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

        if (terrainCreatedSubscription && terrainCreatedSubscription->isValid())
        {
            dispatcher.unsubscribe(*terrainCreatedSubscription);
        }

        if (terrainDeletedSubscription && terrainDeletedSubscription->isValid())
        {
            dispatcher.unsubscribe(*terrainDeletedSubscription);
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

        // Terrain lifecycle coupling — auto-create/delete water with terrain
        auto terrainCreatedToken = dispatcher.subscribe<events::terrain::TerrainCreatedNotification>(
            [this](const events::terrain::TerrainCreatedNotification& notification)
            {
                onTerrainCreated(notification.config, notification.terrainEntity);
            });
        terrainCreatedSubscription = std::make_unique<events::SubscriptionToken>(terrainCreatedToken);

        auto terrainDeletedToken = dispatcher.subscribe<events::terrain::TerrainDeletedNotification>(
            [this](const events::terrain::TerrainDeletedNotification& notification)
            {
                onTerrainDeleted(notification.terrainEntity);
            });
        terrainDeletedSubscription = std::make_unique<events::SubscriptionToken>(terrainDeletedToken);
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
    }

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
        waterComp.globalDensity = 1000.0f;
        waterComp.globalDrag = 0.5f;
        waterComp.globalBuoyancyStrength = 1.0f;
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

            // Create physics sensor body for water detection
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

        scene::Entity waterEnt(entity);
        sceneGraph->removeEntity(waterEnt);

        events::water::WaterDeletedNotification notification;
        notification.waterEntity = waterEntity;
        events::EventDispatcher::instance().publish(notification);

        return true;
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

        // Update ECS component for this tile entity
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WaterTileComponent>();
        for (auto entity : view)
        {
            auto& comp = view.get<components::WaterTileComponent>(entity);
            if (comp.tileX == tileX && comp.tileZ == tileZ)
            {
                comp.waterHeight = height;
                break;
            }
        }
    }

    water::WaterGlobalSettings WaterService::getWaterGlobalSettings() const
    {
        water::WaterGlobalSettings settings;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WaterComponent>();
        for (auto entity : view)
        {
            const auto& comp = view.get<components::WaterComponent>(entity);
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
            break;
        }

        return settings;
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
    }

    void WaterService::onEntityDeleted(EntityHandle entity)
    {
        if (!entity.isValid())
            return;

        // Remove physics sensor body if this was a water tile entity
        if (physicsProvider)
        {
            physicsProvider->removeWaterSensorBody(entity);
        }

        auto it = waterGrids.find(entity.id);
        if (it != waterGrids.end())
        {
            waterGrids.erase(it);
        }
    }

    void WaterService::onSceneCleared()
    {
        entitiesInWater.clear();

        if (waterGrids.empty())
            return;

        waterGrids.clear();
        vfLogInfo("WaterService: Cleared all water on scene clear");
    }

    void WaterService::onTerrainCreated(const TerrainCreationData& config,
                                        EntityHandle /*terrainEntity*/)
    {
        // Don't auto-create if water already exists
        if (!waterGrids.empty())
            return;

        WaterCreationData waterConfig;
        waterConfig.tilesX = config.tilesX;
        waterConfig.tilesZ = config.tilesZ;
        waterConfig.worldTileSize = config.worldTileSize;
        waterConfig.waterHeight = 0.0f;
        waterConfig.waveIntensity = 1.0f;
        waterConfig.physicsEnabled = true;

        createWater(waterConfig);
        vfLogInfo("WaterService: Auto-created water matching terrain grid ({}x{})",
                  config.tilesX, config.tilesZ);
    }

    void WaterService::onTerrainDeleted(EntityHandle /*terrainEntity*/)
    {
        if (waterGrids.empty())
            return;

        // Collect all water entity IDs then delete
        std::vector<EntityHandle> toDelete;
        for (const auto& [entityId, grid] : waterGrids)
        {
            toDelete.push_back(EntityHandle{entityId});
        }

        for (const auto& handle : toDelete)
        {
            deleteWater(handle);
        }

        vfLogInfo("WaterService: Auto-deleted water on terrain deletion");
    }

    void WaterService::updateBuoyancy(float deltaTime)
    {
        if (!physicsProvider || entitiesInWater.empty() || waterGrids.empty())
            return;

        auto settings = getWaterGlobalSettings();
        glm::vec3 gravity = physicsProvider->getGravity();
        float gravityMag = glm::length(gravity);

        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto it = entitiesInWater.begin(); it != entitiesInWater.end(); )
        {
            EntityHandle entity = *it;
            entt::entity ent = internal::fromHandle(entity);

            if (!registry.valid(ent) || !physicsProvider->hasRigidBody(entity))
            {
                it = entitiesInWater.erase(it);
                continue;
            }

            // Skip static bodies
            if (registry.all_of<components::RigidBodyComponent>(ent))
            {
                const auto& rb = registry.get<components::RigidBodyComponent>(ent);
                if (rb.type == components::RigidBodyType::Static)
                {
                    ++it;
                    continue;
                }
            }

            glm::vec3 pos = physicsProvider->getPosition(entity);

            float halfHeight = 0.5f;
            if (registry.all_of<components::ColliderComponent>(ent))
            {
                const auto& collider = registry.get<components::ColliderComponent>(ent);
                switch (collider.shape)
                {
                case components::ColliderShape::Box:
                    halfHeight = collider.size.y;
                    break;
                case components::ColliderShape::Sphere:
                    halfHeight = collider.size.x;
                    break;
                case components::ColliderShape::Capsule:
                    halfHeight = collider.size.x + collider.height * 0.5f;
                    break;
                default:
                    halfHeight = 0.5f;
                    break;
                }
            }

            float waterHeight = getWaterHeightAt(glm::vec2(pos.x, pos.z));
            float objectBottom = pos.y - halfHeight;
            float objectHeight = halfHeight * 2.0f;

            float submergedDepth = glm::clamp(waterHeight - objectBottom, 0.0f, objectHeight);
            float submersionRatio = submergedDepth / objectHeight;

            if (submersionRatio <= 0.0f)
            {
                ++it;
                continue;
            }

            // Get object mass from ECS component
            float mass = 1.0f;
            if (registry.all_of<components::RigidBodyComponent>(ent))
            {
                mass = registry.get<components::RigidBodyComponent>(ent).mass;
            }

            // Mass-proportional buoyancy: at equilibrium submersionRatio = 1/buoyancyStrength
            float buoyancyForce = mass * gravityMag * submersionRatio * settings.buoyancyStrength;
            physicsProvider->applyForce(entity, glm::vec3(0.0f, buoyancyForce, 0.0f));

            // Drag: opposes velocity proportional to submersion
            glm::vec3 velocity = physicsProvider->getLinearVelocity(entity);
            glm::vec3 dragForce = -velocity * settings.drag * submersionRatio * mass;
            physicsProvider->applyForce(entity, dragForce);

            ++it;
        }
    }

    void WaterService::clearBuoyancyTracking()
    {
        entitiesInWater.clear();
    }
}
