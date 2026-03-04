#include "EntityStateService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"

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

        dispatcher.registerQueryHandler<events::scene::GetSelectedEntityQuery>(
            [this](const events::scene::GetSelectedEntityQuery&)
            {
                return getSelectedEntity();
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
    }

    void EntityStateService::setSelectedEntity(std::optional<EntityHandle> entity)
    {
        selectedEntity = entity;

        events::scene::EntitySelectedNotification notification;
        notification.entity = entity;
        events::EventDispatcher::instance().publish(notification);
    }

    std::optional<EntityHandle> EntityStateService::getSelectedEntity() const
    {
        return selectedEntity;
    }

    void EntityStateService::clearSelection()
    {
        selectedEntity = std::nullopt;
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
}
