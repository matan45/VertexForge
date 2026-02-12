#include "EntityQueryService.hpp"
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
    EntityQueryService::EntityQueryService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
    {
    }

    void EntityQueryService::registerEventHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerQueryHandler<events::scene::GetEntityQuery>(
            [this](const events::scene::GetEntityQuery& query)
            {
                return getEntity(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetSceneHierarchyQuery>(
            [this](const events::scene::GetSceneHierarchyQuery&)
            {
                return getSceneHierarchy();
            });

        dispatcher.registerQueryHandler<events::scene::FindEntitiesByNameQuery>(
            [this](const events::scene::FindEntitiesByNameQuery& query)
            {
                return findEntitiesByName(query.name);
            });

        dispatcher.registerQueryHandler<events::scene::GetEntitiesWithComponentQuery>(
            [this](const events::scene::GetEntitiesWithComponentQuery& query)
            {
                return getEntitiesWithComponent(query.componentType);
            });

        dispatcher.registerQueryHandler<events::scene::GetRootEntityQuery>(
            [this](const events::scene::GetRootEntityQuery&)
            {
                return getRoot();
            });
    }

    EntityHandle EntityQueryService::getRoot() const
    {
        return internal::toHandle(sceneGraph->GetRoot().getHandle());
    }

    std::optional<EntityData> EntityQueryService::getEntity(EntityHandle handle) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(handle, registry))
        {
            return std::nullopt;
        }

        return buildEntityData(internal::fromHandle(handle));
    }

    std::optional<EntityHandle> EntityQueryService::findEntityByName(const std::string& name) const
    {
        auto entities = sceneGraph->findAllEntitiesByName(name);
        if (entities.empty())
        {
            return std::nullopt;
        }
        return internal::toHandle(entities[0].getHandle());
    }

    std::vector<EntityHandle> EntityQueryService::findEntitiesByName(const std::string& name) const
    {
        auto entities = sceneGraph->findAllEntitiesByName(name);
        std::vector<EntityHandle> handles;
        handles.reserve(entities.size());
        for (auto& entity : entities)
        {
            handles.push_back(internal::toHandle(entity.getHandle()));
        }
        return handles;
    }

    std::vector<EntityHandle> EntityQueryService::getEntitiesWithComponent(ComponentTypeId type) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<EntityHandle> handles;

        switch (type)
        {
        case ComponentTypeId::Camera:
            {
                auto view = registry.view<components::CameraComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::Transform:
            {
                auto view = registry.view<components::TransformComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::IBL:
            {
                auto view = registry.view<components::IBLComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::Mesh:
            {
                auto view = registry.view<components::MeshComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::AudioSource2D:
            {
                auto view = registry.view<components::AudioSource2DComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::AudioSource3D:
            {
                auto view = registry.view<components::AudioSource3DComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::Script:
            {
                auto view = registry.view<components::ScriptComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::Collider:
            {
                auto view = registry.view<components::ColliderComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::RigidBody:
            {
                auto view = registry.view<components::RigidBodyComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::Animator:
            {
                auto view = registry.view<components::AnimatorComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::VFX:
            {
                auto view = registry.view<components::VFXComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::Text:
            {
                auto view = registry.view<components::TextComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::UICanvas:
            {
                auto view = registry.view<components::UICanvasComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::UIRect:
            {
                auto view = registry.view<components::UIRectComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::UIImage:
            {
                auto view = registry.view<components::UIImageComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        case ComponentTypeId::UIScroll:
            {
                auto view = registry.view<components::UIScrollComponent>();
                for (auto entity : view)
                {
                    handles.push_back(internal::toHandle(entity));
                }
                break;
            }
        default:
            break;
        }

        return handles;
    }

    SceneHierarchyData EntityQueryService::getSceneHierarchy() const
    {
        SceneHierarchyData data;
        data.root = getRoot();

        collectHierarchy(internal::fromHandle(data.root), data.entities);

        return data;
    }

    bool EntityQueryService::hasComponent(EntityHandle entity, ComponentTypeId type) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        auto enttEntity = internal::fromHandle(entity);

        switch (type)
        {
        case ComponentTypeId::Transform:
            return registry.all_of<components::TransformComponent>(enttEntity);
        case ComponentTypeId::Camera:
            return registry.all_of<components::CameraComponent>(enttEntity);
        case ComponentTypeId::Name:
            return registry.all_of<components::NameComponent>(enttEntity);
        case ComponentTypeId::Parent:
            return registry.all_of<components::ParentComponent>(enttEntity);
        case ComponentTypeId::Children:
            return registry.all_of<components::ChildrenComponent>(enttEntity);
        case ComponentTypeId::WorldTransform:
            return registry.all_of<components::WorldTransformComponent>(enttEntity);
        case ComponentTypeId::IBL:
            return registry.all_of<components::IBLComponent>(enttEntity);
        case ComponentTypeId::Mesh:
            return registry.all_of<components::MeshComponent>(enttEntity);
        case ComponentTypeId::AudioSource2D:
            return registry.all_of<components::AudioSource2DComponent>(enttEntity);
        case ComponentTypeId::AudioSource3D:
            return registry.all_of<components::AudioSource3DComponent>(enttEntity);
        case ComponentTypeId::Script:
            return registry.all_of<components::ScriptComponent>(enttEntity);
        case ComponentTypeId::Collider:
            return registry.all_of<components::ColliderComponent>(enttEntity);
        case ComponentTypeId::RigidBody:
            return registry.all_of<components::RigidBodyComponent>(enttEntity);
        case ComponentTypeId::Animator:
            return registry.all_of<components::AnimatorComponent>(enttEntity);
        case ComponentTypeId::VFX:
            return registry.all_of<components::VFXComponent>(enttEntity);
        case ComponentTypeId::Text:
            return registry.all_of<components::TextComponent>(enttEntity);
        case ComponentTypeId::UICanvas:
            return registry.all_of<components::UICanvasComponent>(enttEntity);
        case ComponentTypeId::UIRect:
            return registry.all_of<components::UIRectComponent>(enttEntity);
        case ComponentTypeId::UIImage:
            return registry.all_of<components::UIImageComponent>(enttEntity);
        case ComponentTypeId::UIScroll:
            return registry.all_of<components::UIScrollComponent>(enttEntity);
        default:
            return false;
        }
    }

    std::vector<ComponentTypeId> EntityQueryService::getComponentTypes(EntityHandle entity) const
    {
        std::vector<ComponentTypeId> types;
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return types;
        }

        auto enttEntity = internal::fromHandle(entity);

        if (registry.all_of<components::TransformComponent>(enttEntity))
            types.push_back(ComponentTypeId::Transform);
        if (registry.all_of<components::CameraComponent>(enttEntity))
            types.push_back(ComponentTypeId::Camera);
        if (registry.all_of<components::NameComponent>(enttEntity))
            types.push_back(ComponentTypeId::Name);
        if (registry.all_of<components::ParentComponent>(enttEntity))
            types.push_back(ComponentTypeId::Parent);
        if (registry.all_of<components::ChildrenComponent>(enttEntity))
            types.push_back(ComponentTypeId::Children);
        if (registry.all_of<components::WorldTransformComponent>(enttEntity))
            types.push_back(ComponentTypeId::WorldTransform);
        if (registry.all_of<components::IBLComponent>(enttEntity))
            types.push_back(ComponentTypeId::IBL);
        if (registry.all_of<components::MeshComponent>(enttEntity))
            types.push_back(ComponentTypeId::Mesh);
        if (registry.all_of<components::AudioSource2DComponent>(enttEntity))
            types.push_back(ComponentTypeId::AudioSource2D);
        if (registry.all_of<components::AudioSource3DComponent>(enttEntity))
            types.push_back(ComponentTypeId::AudioSource3D);
        if (registry.all_of<components::ScriptComponent>(enttEntity))
            types.push_back(ComponentTypeId::Script);
        if (registry.all_of<components::ColliderComponent>(enttEntity))
            types.push_back(ComponentTypeId::Collider);
        if (registry.all_of<components::RigidBodyComponent>(enttEntity))
            types.push_back(ComponentTypeId::RigidBody);
        if (registry.all_of<components::AnimatorComponent>(enttEntity))
            types.push_back(ComponentTypeId::Animator);
        if (registry.all_of<components::VFXComponent>(enttEntity))
            types.push_back(ComponentTypeId::VFX);
        if (registry.all_of<components::TextComponent>(enttEntity))
            types.push_back(ComponentTypeId::Text);
        if (registry.all_of<components::UICanvasComponent>(enttEntity))
            types.push_back(ComponentTypeId::UICanvas);
        if (registry.all_of<components::UIRectComponent>(enttEntity))
            types.push_back(ComponentTypeId::UIRect);
        if (registry.all_of<components::UIImageComponent>(enttEntity))
            types.push_back(ComponentTypeId::UIImage);
        if (registry.all_of<components::UIScrollComponent>(enttEntity))
            types.push_back(ComponentTypeId::UIScroll);

        return types;
    }

    EntityData EntityQueryService::buildEntityData(entt::entity entity) const
    {
        scene::Entity sceneEntity(entity);

        EntityData data;
        data.handle = internal::toHandle(entity);
        data.name = sceneEntity.getName();

        if (sceneEntity.hasComponent<components::NameComponent>())
        {
            data.isActive = sceneEntity.getComponent<components::NameComponent>().isActive;
        }

        if (sceneEntity.hasComponent<components::ParentComponent>())
        {
            data.parent = internal::toHandle(sceneEntity.getComponent<components::ParentComponent>().parent);
        }

        if (sceneEntity.hasComponent<components::ChildrenComponent>())
        {
            auto& children = sceneEntity.getComponent<components::ChildrenComponent>().children;
            for (auto child : children)
            {
                data.children.push_back(internal::toHandle(child));
            }
        }

        if (sceneEntity.hasComponent<components::TransformComponent>())
        {
            auto& transform = sceneEntity.getComponent<components::TransformComponent>();
            data.localTransform = TransformData{transform.position, transform.rotation, transform.scale};
        }

        if (sceneEntity.hasComponent<components::WorldTransformComponent>())
        {
            const auto& worldComp = sceneEntity.getComponent<components::WorldTransformComponent>();
            auto decomposed = math::decomposeMatrix(worldComp.worldMatrix);
            data.worldTransform = TransformData{decomposed.position, decomposed.rotation, decomposed.scale};
        }
        else
        {
            // No world transform yet, use local
            data.worldTransform = data.localTransform;
        }

        data.components = getComponentTypes(data.handle);

        return data;
    }

    void EntityQueryService::collectHierarchy(entt::entity entity, std::vector<EntityData>& entities) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(entity)) return;

        entities.push_back(buildEntityData(entity));

        scene::Entity sceneEntity(entity);
        if (sceneEntity.hasComponent<components::ChildrenComponent>())
        {
            auto& children = sceneEntity.getComponent<components::ChildrenComponent>().children;
            for (auto child : children)
            {
                collectHierarchy(child, entities);
            }
        }
    }
}
