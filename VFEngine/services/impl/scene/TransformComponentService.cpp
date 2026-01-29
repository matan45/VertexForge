#include "TransformComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "math/TransformUtils.hpp"
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

        dispatcher.registerCommandHandler<events::scene::SetWorldTransformCommand>(
            [this](const events::scene::SetWorldTransformCommand& cmd)
            {
                setWorldTransform(cmd.entity, cmd.worldTransform);
            });

        dispatcher.registerQueryHandler<events::scene::GetTransformQuery>(
            [this](const events::scene::GetTransformQuery& query)
            {
                return getTransform(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetWorldTransformQuery>(
            [this](const events::scene::GetWorldTransformQuery& query)
            {
                return getWorldTransform(query.entity);
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

            // Mark this entity and all descendants dirty
            sceneGraph->markTransformDirty(sceneEntity);

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
            // No world transform yet, return local (root entity case)
            return getTransform(entity);
        }

        // Decompose the world matrix to get world position, rotation, scale
        const auto& worldComp = sceneEntity.getComponent<components::WorldTransformComponent>();
        auto decomposed = math::decomposeMatrix(worldComp.worldMatrix);

        return TransformData{decomposed.position, decomposed.rotation, decomposed.scale};
    }

    void TransformComponentService::setWorldTransform(EntityHandle entity, const TransformData& worldTransform)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        // Convert world transform to matrix once
        glm::mat4 worldMatrix = math::composeMatrix(
            worldTransform.position,
            worldTransform.rotation,
            worldTransform.scale);

        // Compute the local transform from world transform
        TransformData localTransform = worldTransform;

        if (sceneEntity.hasComponent<components::ParentComponent>())
        {
            auto parentHandle = sceneEntity.getComponent<components::ParentComponent>().parent;
            if (parentHandle != entt::null)
            {
                scene::Entity parentEntity(parentHandle);
                if (parentEntity.hasComponent<components::WorldTransformComponent>())
                {
                    // Get parent's world matrix and invert it
                    const auto& parentWorld = parentEntity.getComponent<components::WorldTransformComponent>().worldMatrix;
                    glm::mat4 parentWorldInverse = glm::inverse(parentWorld);

                    // Local = inverse(parentWorld) * worldMatrix
                    glm::mat4 localMatrix = parentWorldInverse * worldMatrix;

                    // Decompose to get local transform
                    auto decomposed = math::decomposeMatrix(localMatrix);
                    localTransform = TransformData{decomposed.position, decomposed.rotation, decomposed.scale};
                }
            }
        }

        // Set local transform values directly
        if (sceneEntity.hasComponent<components::TransformComponent>())
        {
            auto& comp = sceneEntity.getComponent<components::TransformComponent>();
            comp.position = localTransform.position;
            comp.rotation = localTransform.rotation;
            comp.scale = localTransform.scale;

            // Cache the world matrix directly - avoid recomputation during update
            sceneEntity.addOrReplaceComponent<components::WorldTransformComponent>().worldMatrix = worldMatrix;

            // Mark children dirty (not this entity - world matrix already set)
            for (auto& child : sceneEntity.getChildren()) {
                sceneGraph->markTransformDirty(child);
            }

            events::scene::TransformChangedNotification notification;
            notification.entity = entity;
            notification.newTransform = localTransform;
            events::EventDispatcher::instance().publish(notification);
        }
    }
}
