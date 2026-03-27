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
#include "../../providers/physics/IPhysicsProvider.hpp"

namespace services
{
    OceanService::OceanService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
    {
    }

    OceanService::~OceanService()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.unregisterCommandHandler<events::ocean::CreateOceanCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::DeleteOceanCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SetOceanVisualSettingsCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SetOceanPhysicsSettingsCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SetOceanFFTConfigCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::SaveOceanCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::LoadOceanCommand>();
        dispatcher.unregisterCommandHandler<events::ocean::RebuildOceanFromComponentsCommand>();

        dispatcher.unregisterQueryHandler<events::ocean::GetOceanEntityQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetOceanDataQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::HasOceanComponentQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetOceanFFTConfigQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::IsOceanFFTEnabledQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::GetOceanHeightAtQuery>();
        dispatcher.unregisterQueryHandler<events::ocean::IsPositionInOceanQuery>();

        if (entityDeletedSubscription && entityDeletedSubscription->isValid())
            dispatcher.unsubscribe(*entityDeletedSubscription);

        if (sceneClearedSubscription && sceneClearedSubscription->isValid())
            dispatcher.unsubscribe(*sceneClearedSubscription);
    }

    void OceanService::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        registerOceanCoreHandlers(dispatcher);
        registerOceanQueryHandlers(dispatcher);
        registerOceanFFTHandlers(dispatcher);

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

        dispatcher.registerQueryHandler<events::ocean::IsPositionInOceanQuery>(
            [this](const events::ocean::IsPositionInOceanQuery& query)
            {
                return isPositionInOcean(query.position);
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
        if (!oceanEntity.isValid())
            return false;

        float height = getOceanHeightAt(glm::vec2(worldPos.x, worldPos.z));
        return worldPos.y <= height;
    }

    float OceanService::getOceanHeightAt(const glm::vec2& worldXZ) const
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

    OceanVisualSettings OceanService::getOceanVisualSettings() const
    {
        OceanVisualSettings settings;
        if (!oceanEntity.isValid())
            return settings;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return settings;

        const auto& comp = registry.get<components::OceanComponent>(ent);
        settings.shallowColor = comp.shallowColor;
        settings.deepColor = comp.deepColor;
        settings.maxVisibleDepth = comp.maxVisibleDepth;
        settings.fresnelPower = comp.fresnelPower;
        settings.refractionStrength = comp.refractionStrength;
        settings.refractionChromatic = comp.refractionChromatic;
        settings.refractionDepthScale = comp.refractionDepthScale;
        settings.causticStrength = comp.causticStrength;
        settings.causticDepthFalloff = comp.causticDepthFalloff;

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

        fileData.density = comp.density;
        fileData.drag = comp.drag;
        fileData.buoyancyStrength = comp.buoyancyStrength;

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
            comp.density = fileData.density;
            comp.drag = fileData.drag;
            comp.buoyancyStrength = fileData.buoyancyStrength;
        }

        events::ocean::OceanLoadedNotification notification;
        notification.oceanEntity = handle;
        notification.path = path;
        events::EventDispatcher::instance().publish(notification);

        return handle;
    }

    void OceanService::updateBuoyancy()
    {
        if (!physicsProvider || !oceanEntity.isValid())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(oceanEntity);
        if (!registry.valid(ent) || !registry.all_of<components::OceanComponent>(ent))
            return;

        const auto& comp = registry.get<components::OceanComponent>(ent);
        if (!comp.physicsEnabled)
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

            float halfHeight = 0.5f;
            if (registry.all_of<components::ColliderComponent>(rbEntity))
            {
                const auto& collider = registry.get<components::ColliderComponent>(rbEntity);
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

            float waterHeight = getOceanHeightAt(glm::vec2(pos.x, pos.z));
            float objectBottom = pos.y - halfHeight;
            float objectHeight = halfHeight * 2.0f;

            float submergedDepth = glm::clamp(waterHeight - objectBottom, 0.0f, objectHeight);
            float submersionRatio = submergedDepth / objectHeight;

            // Track enter/exit for submersion notifications
            bool wasInWater = entitiesInWater.contains(handle);
            bool isInWater = submersionRatio > 0.0f;

            if (isInWater && !wasInWater)
                entitiesInWater.insert(handle);
            else if (!isInWater && wasInWater)
                entitiesInWater.erase(handle);

            if (submersionRatio <= 0.0f)
                continue;

            float mass = rb.mass;

            float buoyancyForce = mass * gravityMag * submersionRatio * comp.buoyancyStrength;
            physicsProvider->applyForce(handle, glm::vec3(0.0f, buoyancyForce, 0.0f));

            glm::vec3 velocity = physicsProvider->getLinearVelocity(handle);
            glm::vec3 dragForce = -velocity * comp.drag * submersionRatio * mass;
            physicsProvider->applyForce(handle, dragForce);
        }
    }

    void OceanService::clearBuoyancyTracking()
    {
        entitiesInWater.clear();
    }

    void OceanService::rebuildOceanFromComponents()
    {
        oceanEntity = {};
        entitiesInWater.clear();

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
