#include "LightBVHManager.hpp"
#include "../../../services/events/SceneEvents.hpp"
#include "../../../services/events/LightCullingEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace controllers::offscreen
{
    LightBVHManager::LightBVHManager() = default;

    bool LightBVHManager::hasAnyLightComponent(entt::entity entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        return registry.any_of<components::PointLightComponent,
                               components::SpotLightComponent,
                               components::DirectionalLightComponent>(entity);
    }

    void LightBVHManager::init()
    {
        auto transformToken = events::EventDispatcher::instance().subscribe<
            events::scene::TransformChangedNotification>(
            [this](const events::scene::TransformChangedNotification& notification)
            {
                auto entity = static_cast<entt::entity>(notification.entity.id);

                if (!hasAnyLightComponent(entity))
                {
                    return;
                }

                uint32_t entityId = static_cast<uint32_t>(entity);
                if (lightBVH.isDynamicLight(entityId))
                {
                    lightBVH.markDynamicLightDirty(entityId);
                }
            });
        transformChangedSubscription = events::ScopedSubscription(transformToken);

        auto deletedToken = events::EventDispatcher::instance().subscribe<
            events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& notification)
            {
                uint32_t entityId = static_cast<uint32_t>(notification.entity.id);

                if (lightBVH.isStaticLight(entityId))
                {
                    lightBVH.markStaticDirty();
                }
                else if (lightBVH.isDynamicLight(entityId))
                {
                    lightBVH.markDynamicDirty();
                }
            });
        entityDeletedSubscription = events::ScopedSubscription(deletedToken);

        auto staticChangedToken = events::EventDispatcher::instance().subscribe<
            events::scene::EntityStaticChangedNotification>(
            [this](const events::scene::EntityStaticChangedNotification& notification)
            {
                auto entity = static_cast<entt::entity>(notification.entity.id);

                if (!hasAnyLightComponent(entity))
                {
                    return;
                }

                lightBVH.markDirty();
            });
        entityStaticChangedSubscription = events::ScopedSubscription(staticChangedToken);

        auto lightDataToken = events::EventDispatcher::instance().subscribe<
            events::lighting::LightDataChangedNotification>(
            [this](const events::lighting::LightDataChangedNotification& notification)
            {
                auto entity = static_cast<entt::entity>(notification.entity.id);
                uint32_t entityId = static_cast<uint32_t>(entity);

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

        auto lightChangedToken = events::EventDispatcher::instance().subscribe<
            events::lighting::LightComponentChangedNotification>(
            [this](const events::lighting::LightComponentChangedNotification& notification)
            {
                uint32_t entityId = static_cast<uint32_t>(notification.entity.id);

                if (notification.added)
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto entity = static_cast<entt::entity>(entityId);

                    if (!registry.valid(entity) ||
                        !registry.all_of<components::TransformComponent>(entity))
                    {
                        return;
                    }

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
                else
                {
                    if (lightBVH.isStaticLight(entityId))
                    {
                        lightBVH.markStaticDirty();
                    }
                    else if (lightBVH.isDynamicLight(entityId))
                    {
                        lightBVH.markDynamicDirty();
                    }
                    else
                    {
                        lightBVH.markDirty();
                    }
                }
            });
        lightComponentChangedSubscription = events::ScopedSubscription(lightChangedToken);

        auto sceneLoadedToken = events::EventDispatcher::instance().subscribe<
            events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification&)
            {
                lightBVH.markDirty();
            });
        sceneLoadedSubscription = events::ScopedSubscription(sceneLoadedToken);

        auto sceneClearedToken = events::EventDispatcher::instance().subscribe<
            events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                lightBVH.markDirty();
            });
        sceneClearedSubscription = events::ScopedSubscription(sceneClearedToken);

        auto prefabToken = events::EventDispatcher::instance().subscribe<
            events::scene::PrefabInstantiatedNotification>(
            [this](const events::scene::PrefabInstantiatedNotification&)
            {
                lightBVH.markDirty();
            });
        prefabInstantiatedSubscription = events::ScopedSubscription(prefabToken);

        auto duplicatedToken = events::EventDispatcher::instance().subscribe<
            events::scene::EntityDuplicatedNotification>(
            [this](const events::scene::EntityDuplicatedNotification& notification)
            {
                auto entity = static_cast<entt::entity>(notification.duplicatedEntity.id);

                if (!hasAnyLightComponent(entity))
                {
                    return;
                }

                auto& registry = scene::EntityRegistry::getRegistry();
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

    void LightBVHManager::update()
    {
        lightBVH.rebuildStaticIfDirty();
        lightBVH.rebuildDynamicIfDirty();
    }
}
