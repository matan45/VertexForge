#include "LightBVHManager.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/SceneEvents.hpp"
#include "../../../services/events/LightCullingEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace controllers::offscreen
{
    LightBVHManager::LightBVHManager() = default;

    LightBVHManager::~LightBVHManager()
    {
        cleanUp();
    }

    // Helper function to check if an entity has any light component
    static bool hasAnyLightComponent(entt::registry& registry, entt::entity entity)
    {
        return registry.any_of<components::PointLightComponent,
                               components::SpotLightComponent,
                               components::DirectionalLightComponent>(entity);
    }

    void LightBVHManager::init()
    {
        // Subscribe to transform changes - used to update dynamic light positions
        auto transformToken = events::EventDispatcher::instance().subscribe<
            events::scene::TransformChangedNotification>(
            [this](const events::scene::TransformChangedNotification& notification)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(notification.entity.id);

                if (!registry.valid(entity) || !hasAnyLightComponent(registry, entity))
                {
                    return;
                }

                // Check if it's a dynamic light and mark it dirty for refit
                uint32_t entityId = static_cast<uint32_t>(entity);
                if (lightBVH.isDynamicLight(entityId))
                {
                    lightBVH.markDynamicLightDirty(entityId);
                }
            });
        transformChangedSubscription = std::make_unique<events::SubscriptionToken>(transformToken);

        // Subscribe to entity deleted notification
        auto deletedToken = events::EventDispatcher::instance().subscribe<
            events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& notification)
            {
                uint32_t entityId = static_cast<uint32_t>(notification.entity.id);

                // Check which tree the light was in and mark it dirty
                if (lightBVH.isStaticLight(entityId))
                {
                    lightBVH.markStaticDirty();
                }
                else if (lightBVH.isDynamicLight(entityId))
                {
                    lightBVH.markDynamicDirty();
                }
            });
        entityDeletedSubscription = std::make_unique<events::SubscriptionToken>(deletedToken);

        // Subscribe to entity static changed notification
        auto staticChangedToken = events::EventDispatcher::instance().subscribe<
            events::scene::EntityStaticChangedNotification>(
            [this](const events::scene::EntityStaticChangedNotification& notification)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(notification.entity.id);

                if (!registry.valid(entity) || !hasAnyLightComponent(registry, entity))
                {
                    return;
                }

                // Light moved between static and dynamic trees - mark both as dirty
                lightBVH.markDirty();
            });
        entityStaticChangedSubscription = std::make_unique<events::SubscriptionToken>(staticChangedToken);

        // Subscribe to light data changed notification (from our new events)
        auto lightDataToken = events::EventDispatcher::instance().subscribe<
            events::lighting::LightDataChangedNotification>(
            [this](const events::lighting::LightDataChangedNotification& notification)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(notification.entity.id);

                if (!registry.valid(entity))
                {
                    return;
                }

                uint32_t entityId = static_cast<uint32_t>(entity);

                // Light properties changed - may affect bounds (e.g., radius, range, angle)
                if (lightBVH.isStaticLight(entityId))
                {
                    lightBVH.markStaticDirty();
                }
                else if (lightBVH.isDynamicLight(entityId))
                {
                    lightBVH.markDynamicLightDirty(entityId);
                }
            });
        lightDataChangedSubscription = std::make_unique<events::SubscriptionToken>(lightDataToken);

        // Subscribe to light component added notification
        auto lightAddedToken = events::EventDispatcher::instance().subscribe<
            events::lighting::LightComponentChangedNotification>(
            [this](const events::lighting::LightComponentChangedNotification& notification)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(notification.entity.id);

                if (!registry.valid(entity))
                {
                    return;
                }

                // Check if entity is static or dynamic
                if (registry.all_of<components::TransformComponent>(entity))
                {
                    const auto& transform = registry.get<components::TransformComponent>(entity);
                    if (transform.isStatic)
                    {
                        lightBVH.markStaticDirty();
                    }
                    else
                    {
                        lightBVH.markDynamicDirty();
                    }
                }
            });
        lightComponentAddedSubscription = std::make_unique<events::SubscriptionToken>(lightAddedToken);

        // Subscribe to scene loaded notification - rebuild BVH when a new scene is loaded
        auto sceneLoadedToken = events::EventDispatcher::instance().subscribe<
            events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification&)
            {
                // Entire scene changed - rebuild both trees
                lightBVH.markDirty();
            });
        sceneLoadedSubscription = std::make_unique<events::SubscriptionToken>(sceneLoadedToken);

        // Subscribe to scene cleared notification - clear BVH when scene is cleared
        auto sceneClearedToken = events::EventDispatcher::instance().subscribe<
            events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                // Scene cleared - rebuild both trees (will be empty)
                lightBVH.markDirty();
            });
        sceneClearedSubscription = std::make_unique<events::SubscriptionToken>(sceneClearedToken);

        // Subscribe to prefab instantiated notification - prefab may contain lights
        auto prefabToken = events::EventDispatcher::instance().subscribe<
            events::scene::PrefabInstantiatedNotification>(
            [this](const events::scene::PrefabInstantiatedNotification&)
            {
                // Prefab may contain lights - mark both trees dirty
                lightBVH.markDirty();
            });
        prefabInstantiatedSubscription = std::make_unique<events::SubscriptionToken>(prefabToken);

        // Subscribe to entity duplicated notification - duplicated entity may have lights
        auto duplicatedToken = events::EventDispatcher::instance().subscribe<
            events::scene::EntityDuplicatedNotification>(
            [this](const events::scene::EntityDuplicatedNotification& notification)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(notification.duplicatedEntity.id);

                if (!registry.valid(entity) || !hasAnyLightComponent(registry, entity))
                {
                    return;
                }

                // Duplicated entity has lights - mark appropriate tree dirty
                if (registry.all_of<components::TransformComponent>(entity))
                {
                    const auto& transform = registry.get<components::TransformComponent>(entity);
                    if (transform.isStatic)
                    {
                        lightBVH.markStaticDirty();
                    }
                    else
                    {
                        lightBVH.markDynamicDirty();
                    }
                }
            });
        entityDuplicatedSubscription = std::make_unique<events::SubscriptionToken>(duplicatedToken);
    }

    void LightBVHManager::cleanUp()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (transformChangedSubscription && transformChangedSubscription->isValid())
        {
            dispatcher.unsubscribe(*transformChangedSubscription);
        }
        if (entityDeletedSubscription && entityDeletedSubscription->isValid())
        {
            dispatcher.unsubscribe(*entityDeletedSubscription);
        }
        if (entityStaticChangedSubscription && entityStaticChangedSubscription->isValid())
        {
            dispatcher.unsubscribe(*entityStaticChangedSubscription);
        }
        if (lightDataChangedSubscription && lightDataChangedSubscription->isValid())
        {
            dispatcher.unsubscribe(*lightDataChangedSubscription);
        }
        if (lightComponentAddedSubscription && lightComponentAddedSubscription->isValid())
        {
            dispatcher.unsubscribe(*lightComponentAddedSubscription);
        }
        if (sceneLoadedSubscription && sceneLoadedSubscription->isValid())
        {
            dispatcher.unsubscribe(*sceneLoadedSubscription);
        }
        if (sceneClearedSubscription && sceneClearedSubscription->isValid())
        {
            dispatcher.unsubscribe(*sceneClearedSubscription);
        }
        if (prefabInstantiatedSubscription && prefabInstantiatedSubscription->isValid())
        {
            dispatcher.unsubscribe(*prefabInstantiatedSubscription);
        }
        if (entityDuplicatedSubscription && entityDuplicatedSubscription->isValid())
        {
            dispatcher.unsubscribe(*entityDuplicatedSubscription);
        }
    }

    void LightBVHManager::rebuild()
    {
        lightBVH.rebuildAll();
    }

    void LightBVHManager::markDirty()
    {
        lightBVH.markDirty();
    }

    void LightBVHManager::update()
    {
        // Rebuild static BVH if dirty (rare - only when static lights change)
        lightBVH.rebuildStaticIfDirty();

        // Update dynamic BVH (refit or rebuild based on structural changes)
        lightBVH.rebuildDynamicIfDirty();
    }
}
