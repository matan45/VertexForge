#include "SceneBVHManager.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/project/SceneEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "material/MaterialTypes.hpp"

namespace controllers::offscreen
{
    SceneBVHManager::SceneBVHManager() = default;

    SceneBVHManager::~SceneBVHManager()
    {
        cleanUp();
    }

    void SceneBVHManager::init(render::RenderPassHandler* renderHandler)
    {
        sceneBVH.setMeshBoundsCallback([renderHandler](const std::string& meshPath) -> const math::AABB*
        {
            auto* meshPipeline = renderHandler->getMeshPipeline();
            if (meshPipeline)
            {
                return meshPipeline->getMeshBoundingBox(meshPath);
            }
            return nullptr;
        });

        auto meshChangedToken = events::EventDispatcher::instance().subscribe<
            events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(notification.entity.id);
                if (registry.valid(entity) && registry.all_of<components::TransformComponent>(entity))
                {
                    const auto& transform = registry.get<components::TransformComponent>(entity);
                    if (transform.isStatic)
                    {
                        sceneBVH.markStaticDirty();
                    }
                    else
                    {
                        sceneBVH.markDynamicDirty();
                    }
                }
            });
        meshDataChangedSubscription = std::make_unique<events::SubscriptionToken>(meshChangedToken);

        auto deletedToken = events::EventDispatcher::instance().subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& notification)
            {
                uint32_t entityId = static_cast<uint32_t>(notification.entity.id);
                if (sceneBVH.isStaticEntity(entityId))
                {
                    sceneBVH.markStaticDirty();
                }
                else
                {
                    sceneBVH.markDynamicDirty();
                }
            });
        entityDeletedSubscription = std::make_unique<events::SubscriptionToken>(deletedToken);

        auto staticChangedToken = events::EventDispatcher::instance().subscribe<
            events::scene::EntityStaticChangedNotification>(
            [this](const events::scene::EntityStaticChangedNotification&)
            {
                sceneBVH.markDirty();
            });
        entityStaticChangedSubscription = std::make_unique<events::SubscriptionToken>(staticChangedToken);

        auto sceneLoadedToken = events::EventDispatcher::instance().subscribe<
            events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification&)
            {
                sceneBVH.markDirty();
            });
        sceneLoadedSubscription = std::make_unique<events::SubscriptionToken>(sceneLoadedToken);

        auto sceneClearedToken = events::EventDispatcher::instance().subscribe<
            events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                sceneBVH.markDirty();
            });
        sceneClearedSubscription = std::make_unique<events::SubscriptionToken>(sceneClearedToken);

        auto prefabToken = events::EventDispatcher::instance().subscribe<
            events::scene::PrefabInstantiatedNotification>(
            [this](const events::scene::PrefabInstantiatedNotification&)
            {
                sceneBVH.markDirty();
            });
        prefabInstantiatedSubscription = std::make_unique<events::SubscriptionToken>(prefabToken);

        auto duplicatedToken = events::EventDispatcher::instance().subscribe<
            events::scene::EntityDuplicatedNotification>(
            [this](const events::scene::EntityDuplicatedNotification& notification)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(notification.duplicatedEntity.id);

                if (!registry.valid(entity) || !registry.all_of<components::MeshComponent>(entity))
                {
                    return;
                }

                if (registry.all_of<components::TransformComponent>(entity))
                {
                    const auto& transform = registry.get<components::TransformComponent>(entity);
                    if (transform.isStatic)
                    {
                        sceneBVH.markStaticDirty();
                    }
                    else
                    {
                        sceneBVH.markDynamicDirty();
                    }
                }
            });
        entityDuplicatedSubscription = std::make_unique<events::SubscriptionToken>(duplicatedToken);
    }

    void SceneBVHManager::cleanUp()
    {
        if (meshDataChangedSubscription && meshDataChangedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*meshDataChangedSubscription);
        }
        if (entityDeletedSubscription && entityDeletedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*entityDeletedSubscription);
        }
        if (entityStaticChangedSubscription && entityStaticChangedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*entityStaticChangedSubscription);
        }
        if (sceneLoadedSubscription && sceneLoadedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*sceneLoadedSubscription);
        }
        if (sceneClearedSubscription && sceneClearedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*sceneClearedSubscription);
        }
        if (prefabInstantiatedSubscription && prefabInstantiatedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*prefabInstantiatedSubscription);
        }
        if (entityDuplicatedSubscription && entityDuplicatedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*entityDuplicatedSubscription);
        }
    }

    void SceneBVHManager::rebuild()
    {
        sceneBVH.rebuildAll();
    }

    void SceneBVHManager::markDirty()
    {
        sceneBVH.markDirty();
    }

}
