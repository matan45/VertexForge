#include "LightBVHManager.hpp"
#include "../../../services/events/SceneEvents.hpp"
#include "../../../services/events/LightCullingEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace controllers::offscreen
{
    LightBVHManager::LightBVHManager() = default;

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
        transformChangedSubscription = events::ScopedSubscription(transformToken);

        // Subscribe to entity deleted notification
        // NOTE: This handles the case where an entire entity is deleted.
        // If a light component was explicitly removed first (via LightComponentChangedNotification),
        // this may mark dirty redundantly, but that's harmless (idempotent boolean flags).
        // EnTT's internal component destruction during entity deletion does NOT go through
        // our service layer, so LightComponentChangedNotification won't fire in that case.
        auto deletedToken = events::EventDispatcher::instance().subscribe<
            events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& notification)
            {
                uint32_t entityId = static_cast<uint32_t>(notification.entity.id);

                // Check which tree the light was in and mark it dirty
                // This uses BVH membership since the entity/components are already gone
                if (lightBVH.isStaticLight(entityId))
                {
                    lightBVH.markStaticDirty();
                }
                else if (lightBVH.isDynamicLight(entityId))
                {
                    lightBVH.markDynamicDirty();
                }
                // If not found in either tree, entity had no light or was already handled
            });
        entityDeletedSubscription = events::ScopedSubscription(deletedToken);

        // Subscribe to entity static changed notification
        // This is a STRUCTURAL change - the light moves between static and dynamic trees
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

                // Light moves between static and dynamic trees - this is a structural change
                // requiring full rebuild of both trees (not just refit)
                lightBVH.markDirty();
            });
        entityStaticChangedSubscription = events::ScopedSubscription(staticChangedToken);

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
        lightDataChangedSubscription = events::ScopedSubscription(lightDataToken);

        // Subscribe to light component changed notification (handles both add and remove)
        // NOTE: This handles explicit component add/remove via service layer.
        // Distinction from EntityDeletedNotification:
        // - LightComponentChangedNotification: component removed, entity still exists
        // - EntityDeletedNotification: entire entity deleted (EnTT destroys components internally)
        auto lightChangedToken = events::EventDispatcher::instance().subscribe<
            events::lighting::LightComponentChangedNotification>(
            [this](const events::lighting::LightComponentChangedNotification& notification)
            {
                uint32_t entityId = static_cast<uint32_t>(notification.entity.id);

                if (notification.added)
                {
                    // Light component added - check TransformComponent to determine tree
                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto entity = static_cast<entt::entity>(entityId);

                    if (!registry.valid(entity))
                    {
                        return;
                    }

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
                }
                else
                {
                    // Light component removed - check which BVH the light was in
                    // We must use BVH membership since the component is already gone
                    if (lightBVH.isStaticLight(entityId))
                    {
                        lightBVH.markStaticDirty();
                    }
                    else if (lightBVH.isDynamicLight(entityId))
                    {
                        lightBVH.markDynamicDirty();
                    }
                    // Note: If entity wasn't in any BVH, it's a no-op (e.g., directional light)
                    // For directional lights, mark both dirty to be safe
                    else
                    {
                        lightBVH.markDirty();
                    }
                }
            });
        lightComponentChangedSubscription = events::ScopedSubscription(lightChangedToken);

        // Subscribe to scene loaded notification - rebuild BVH when a new scene is loaded
        auto sceneLoadedToken = events::EventDispatcher::instance().subscribe<
            events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification&)
            {
                // Entire scene changed - rebuild both trees
                lightBVH.markDirty();
            });
        sceneLoadedSubscription = events::ScopedSubscription(sceneLoadedToken);

        // Subscribe to scene cleared notification - clear BVH when scene is cleared
        auto sceneClearedToken = events::EventDispatcher::instance().subscribe<
            events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                // Scene cleared - rebuild both trees (will be empty)
                lightBVH.markDirty();
            });
        sceneClearedSubscription = events::ScopedSubscription(sceneClearedToken);

        // Subscribe to prefab instantiated notification - prefab may contain lights
        auto prefabToken = events::EventDispatcher::instance().subscribe<
            events::scene::PrefabInstantiatedNotification>(
            [this](const events::scene::PrefabInstantiatedNotification&)
            {
                // Prefab may contain lights - mark both trees dirty
                lightBVH.markDirty();
            });
        prefabInstantiatedSubscription = events::ScopedSubscription(prefabToken);

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
        entityDuplicatedSubscription = events::ScopedSubscription(duplicatedToken);
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
