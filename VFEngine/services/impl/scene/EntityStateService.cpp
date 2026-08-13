#include "EntityStateService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"
#include <unordered_set>

namespace services
{
    EntityStateService::EntityStateService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
    {
    }

    void EntityStateService::registerEventHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::scene::SelectEntityCommand>(
            [this](const events::scene::SelectEntityCommand& cmd)
            {
                setSelectedEntity(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SelectEntitiesCommand>(
            [this](const events::scene::SelectEntitiesCommand& cmd)
            {
                setSelectedEntities(cmd.entities);
            });

        dispatcher.registerQueryHandler<events::scene::GetSelectedEntityQuery>(
            [this](const events::scene::GetSelectedEntityQuery&)
            {
                return getSelectedEntity();
            });

        dispatcher.registerQueryHandler<events::scene::GetSelectedEntitiesQuery>(
            [this](const events::scene::GetSelectedEntitiesQuery&)
            {
                return getSelectedEntities();
            });

        dispatcher.registerCommandHandler<events::scene::SetEntityNameCommand>(
            [this](const events::scene::SetEntityNameCommand& cmd)
            {
                setEntityName(cmd.entity, cmd.newName);
            });

        dispatcher.registerCommandHandler<events::scene::SetEntityActiveCommand>(
            [this](const events::scene::SetEntityActiveCommand& cmd)
            {
                setEntityActive(cmd.entity, cmd.isActive);
            });

        dispatcher.registerCommandHandler<events::scene::MarkPreviewSandboxCommand>(
            [this](const events::scene::MarkPreviewSandboxCommand& cmd)
            {
                return markPreviewSandbox(cmd.entity, cmd.tagged);
            });

        dispatcher.registerCommandHandler<events::scene::SetEntityStaticCommand>(
            [this](const events::scene::SetEntityStaticCommand& cmd)
            {
                return setEntityStatic(cmd.entity, cmd.isStatic);
            });

        dispatcher.registerQueryHandler<events::scene::IsEntityStaticQuery>(
            [this](const events::scene::IsEntityStaticQuery& query)
            {
                return isEntityStatic(query.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetEntitySpatiallyLoadedCommand>(
            [this](const events::scene::SetEntitySpatiallyLoadedCommand& cmd)
            {
                return setEntitySpatiallyLoaded(cmd.entity, cmd.spatiallyLoaded);
            });

        dispatcher.registerQueryHandler<events::scene::IsEntitySpatiallyLoadedQuery>(
            [this](const events::scene::IsEntitySpatiallyLoadedQuery& query)
            {
                return isEntitySpatiallyLoaded(query.entity);
            });
    }

    void EntityStateService::setSelectedEntity(std::optional<EntityHandle> entity)
    {
        std::vector<EntityHandle> entities;
        if (entity.has_value())
        {
            entities.push_back(*entity);
        }
        setSelectedEntities(std::move(entities));
    }

    void EntityStateService::setSelectedEntities(std::vector<EntityHandle> entities)
    {
        // VK-1490: every selection write (Scene Graph, Viewport, play-mode
        // reselect) funnels through here, so invalid, deleted and duplicate
        // handles are pruned once, centrally. Order is preserved (first
        // occurrence wins) and front() after pruning is the active entity.
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<EntityHandle> normalized;
        normalized.reserve(entities.size());
        std::unordered_set<uint64_t> seen;
        for (const auto& handle : entities)
        {
            if (!internal::isValidHandle(handle, registry)) continue;
            if (seen.insert(handle.id).second)
            {
                normalized.push_back(handle);
            }
        }
        selectedEntities = std::move(normalized);

        // Single-selection consumers (gizmo, details panel) track the primary entity.
        events::scene::EntitySelectedNotification notification;
        notification.entity = selectedEntities.empty()
                                  ? std::nullopt
                                  : std::optional{selectedEntities.front()};
        events::EventDispatcher::instance().publish(notification);
    }

    std::optional<EntityHandle> EntityStateService::getSelectedEntity() const
    {
        if (selectedEntities.empty())
        {
            return std::nullopt;
        }
        return selectedEntities.front();
    }

    const std::vector<EntityHandle>& EntityStateService::getSelectedEntities() const
    {
        return selectedEntities;
    }

    void EntityStateService::clearSelection()
    {
        selectedEntities.clear();
    }

    void EntityStateService::setEntityName(EntityHandle entity, const std::string& name)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        sceneEntity.setName(name);
    }

    std::string EntityStateService::getEntityName(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return "";
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.getName();
    }

    void EntityStateService::setEntityActive(EntityHandle entity, bool isActive)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::NameComponent>())
        {
            sceneEntity.getComponent<components::NameComponent>().isActive = isActive;
        }
    }

    bool EntityStateService::markPreviewSandbox(EntityHandle entity, bool tagged)
    {
        // VK-1433 Phase 4 — mirror UIComponentService::markUIPreviewSandbox. Tagging adds the
        // marker AND marks the root inactive: the scene serializer skips a tagged subtree, and an
        // inactive root keeps the entity out of the main edit/play passes. The Prefab Rig Preview
        // renders the entities directly (it does not gate on isActive), so the inactive flag never
        // affects the offscreen preview. Untagging restores the active flag.
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        entt::entity ent = internal::fromHandle(entity);
        if (tagged)
        {
            registry.emplace_or_replace<components::PreviewSandboxTagComponent>(ent);
            if (registry.all_of<components::NameComponent>(ent))
            {
                registry.get<components::NameComponent>(ent).isActive = false;
            }
        }
        else
        {
            registry.remove<components::PreviewSandboxTagComponent>(ent);
            if (registry.all_of<components::NameComponent>(ent))
            {
                registry.get<components::NameComponent>(ent).isActive = true;
            }
        }
        return true;
    }

    bool EntityStateService::setEntityStatic(EntityHandle entity, bool isStatic)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        auto enttEntity = internal::fromHandle(entity);
        if (!registry.all_of<components::TransformComponent>(enttEntity))
        {
            return false;
        }

        auto& transform = registry.get<components::TransformComponent>(enttEntity);
        bool wasStatic = transform.isStatic;

        if (isStatic == wasStatic)
        {
            return true;
        }

        transform.isStatic = isStatic;

        events::scene::EntityStaticChangedNotification notification;
        notification.entity = entity;
        notification.isStatic = isStatic;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    bool EntityStateService::isEntityStatic(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return true;
        }

        auto enttEntity = internal::fromHandle(entity);
        if (!registry.all_of<components::TransformComponent>(enttEntity))
        {
            return true;
        }

        const auto& transform = registry.get<components::TransformComponent>(enttEntity);
        return transform.isStatic;
    }

    bool EntityStateService::setEntitySpatiallyLoaded(EntityHandle entity, bool spatiallyLoaded)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        auto enttEntity = internal::fromHandle(entity);
        auto* policy = registry.try_get<components::StreamingPolicyComponent>(enttEntity);

        // Absence is the "spatially loaded" default, so setting true on an entity that never
        // carried the component is a genuine no-op - do not mint one just to store the default.
        if (!policy)
        {
            if (spatiallyLoaded)
            {
                return true;
            }
            policy = &registry.emplace<components::StreamingPolicyComponent>(enttEntity);
        }
        else if (policy->spatiallyLoaded == spatiallyLoaded)
        {
            return true;
        }

        policy->spatiallyLoaded = spatiallyLoaded;

        events::scene::EntityStreamingPolicyChangedNotification notification;
        notification.entity = entity;
        notification.spatiallyLoaded = spatiallyLoaded;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    bool EntityStateService::isEntitySpatiallyLoaded(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return true;
        }

        const auto* policy =
            registry.try_get<components::StreamingPolicyComponent>(internal::fromHandle(entity));
        return policy ? policy->spatiallyLoaded : true;
    }
}
