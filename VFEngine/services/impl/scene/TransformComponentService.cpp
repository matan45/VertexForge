#include "TransformComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/SceneEvents.hpp"

namespace services
{
    TransformComponentService::TransformComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
    {
    }

    void TransformComponentService::registerEventHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::scene::SetTransformCommand>(
            [this](const events::scene::SetTransformCommand& cmd)
            {
                setTransform(cmd.entity, cmd.transform);
            });

        dispatcher.registerQueryHandler<events::scene::GetTransformQuery>(
            [this](const events::scene::GetTransformQuery& query)
            {
                return getTransform(query.entity);
            });
    }

    void TransformComponentService::setTransform(EntityHandle entity, const TransformData& transform)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::TransformComponent>())
        {
            auto& comp = sceneEntity.getComponent<components::TransformComponent>();
            comp.position = transform.position;
            comp.rotation = transform.rotation;
            comp.scale = transform.scale;
            comp.isDirty = true;

            events::scene::TransformChangedNotification notification;
            notification.entity = entity;
            notification.newTransform = transform;
            events::EventDispatcher::instance().publish(notification);
        }
    }

    std::optional<TransformData> TransformComponentService::getTransform(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::TransformComponent>())
        {
            return std::nullopt;
        }

        auto& comp = sceneEntity.getComponent<components::TransformComponent>();
        return TransformData{comp.position, comp.rotation, comp.scale};
    }

    std::optional<TransformData> TransformComponentService::getWorldTransform(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::WorldTransformComponent>())
        {
            return getTransform(entity);
        }

        return getTransform(entity);
    }
}
