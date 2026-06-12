#include "EntityQueryService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "math/TransformUtils.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"

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

#define COLLECT_CASE(TypeId, CompType) \
        case ComponentTypeId::TypeId: \
            for (auto e : registry.view<components::CompType>()) \
                handles.push_back(internal::toHandle(e)); \
            break

        switch (type)
        {
            COLLECT_CASE(Camera, CameraComponent);
            COLLECT_CASE(Transform, TransformComponent);
            COLLECT_CASE(IBL, IBLComponent);
            COLLECT_CASE(Mesh, MeshComponent);
            COLLECT_CASE(AudioSource2D, AudioSource2DComponent);
            COLLECT_CASE(AudioSource3D, AudioSource3DComponent);
            COLLECT_CASE(Script, ScriptComponent);
            COLLECT_CASE(Collider, ColliderComponent);
            COLLECT_CASE(RigidBody, RigidBodyComponent);
            COLLECT_CASE(PhysicsAnimation, PhysicsAnimationComponent);
            COLLECT_CASE(Animator, AnimatorComponent);
            COLLECT_CASE(VFX, VFXComponent);
            COLLECT_CASE(Text, TextComponent);
            COLLECT_CASE(UICanvas, UICanvasComponent);
            COLLECT_CASE(UIRect, UIRectComponent);
            COLLECT_CASE(UIImage, UIImageComponent);
            COLLECT_CASE(UIScroll, UIScrollComponent);
            COLLECT_CASE(UILayoutGroup, UILayoutGroupComponent);
            COLLECT_CASE(UILabel, UILabelComponent);
            COLLECT_CASE(UIButton, UIButtonComponent);
            COLLECT_CASE(UITextInput, UITextInputComponent);
            COLLECT_CASE(UICheckbox, UICheckboxComponent);
            COLLECT_CASE(UIDropdown, UIDropdownComponent);
            COLLECT_CASE(UITabs, UITabsComponent);
            COLLECT_CASE(UISlider, UISliderComponent);
            COLLECT_CASE(UIProgressBar, UIProgressBarComponent);
            COLLECT_CASE(UIStyle, UIStyleComponent);
            COLLECT_CASE(RenderTexture, RenderTextureComponent);
            COLLECT_CASE(Decal, DecalComponent);
        default:
            break;
        }

#undef COLLECT_CASE

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

#define HAS_CASE(TypeId, CompType) \
        case ComponentTypeId::TypeId: return registry.all_of<components::CompType>(enttEntity)

        switch (type)
        {
            HAS_CASE(Transform, TransformComponent);
            HAS_CASE(Camera, CameraComponent);
            HAS_CASE(Name, NameComponent);
            HAS_CASE(Parent, ParentComponent);
            HAS_CASE(Children, ChildrenComponent);
            HAS_CASE(WorldTransform, WorldTransformComponent);
            HAS_CASE(IBL, IBLComponent);
            HAS_CASE(Mesh, MeshComponent);
            HAS_CASE(AudioSource2D, AudioSource2DComponent);
            HAS_CASE(AudioSource3D, AudioSource3DComponent);
            HAS_CASE(Script, ScriptComponent);
            HAS_CASE(Collider, ColliderComponent);
            HAS_CASE(RigidBody, RigidBodyComponent);
            HAS_CASE(PhysicsAnimation, PhysicsAnimationComponent);
            HAS_CASE(Animator, AnimatorComponent);
            HAS_CASE(VFX, VFXComponent);
            HAS_CASE(Text, TextComponent);
            HAS_CASE(UICanvas, UICanvasComponent);
            HAS_CASE(UIRect, UIRectComponent);
            HAS_CASE(UIImage, UIImageComponent);
            HAS_CASE(UIScroll, UIScrollComponent);
            HAS_CASE(UILayoutGroup, UILayoutGroupComponent);
            HAS_CASE(UILabel, UILabelComponent);
            HAS_CASE(UIButton, UIButtonComponent);
            HAS_CASE(UITextInput, UITextInputComponent);
            HAS_CASE(UICheckbox, UICheckboxComponent);
            HAS_CASE(UIDropdown, UIDropdownComponent);
            HAS_CASE(UITabs, UITabsComponent);
            HAS_CASE(UISlider, UISliderComponent);
            HAS_CASE(UIProgressBar, UIProgressBarComponent);
            HAS_CASE(UIStyle, UIStyleComponent);
            HAS_CASE(RenderTexture, RenderTextureComponent);
            HAS_CASE(Decal, DecalComponent);
        default:
            return false;
        }

#undef HAS_CASE
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

#define CHECK_COMP(TypeId, CompType) \
        if (registry.all_of<components::CompType>(enttEntity)) \
            types.push_back(ComponentTypeId::TypeId)

        CHECK_COMP(Transform, TransformComponent);
        CHECK_COMP(Camera, CameraComponent);
        CHECK_COMP(Name, NameComponent);
        CHECK_COMP(Parent, ParentComponent);
        CHECK_COMP(Children, ChildrenComponent);
        CHECK_COMP(WorldTransform, WorldTransformComponent);
        CHECK_COMP(IBL, IBLComponent);
        CHECK_COMP(Mesh, MeshComponent);
        CHECK_COMP(AudioSource2D, AudioSource2DComponent);
        CHECK_COMP(AudioSource3D, AudioSource3DComponent);
        CHECK_COMP(Script, ScriptComponent);
        CHECK_COMP(Collider, ColliderComponent);
        CHECK_COMP(RigidBody, RigidBodyComponent);
        CHECK_COMP(PhysicsAnimation, PhysicsAnimationComponent);
        CHECK_COMP(Animator, AnimatorComponent);
        CHECK_COMP(VFX, VFXComponent);
        CHECK_COMP(Text, TextComponent);
        CHECK_COMP(UICanvas, UICanvasComponent);
        CHECK_COMP(UIRect, UIRectComponent);
        CHECK_COMP(UIImage, UIImageComponent);
        CHECK_COMP(UIScroll, UIScrollComponent);
        CHECK_COMP(UILayoutGroup, UILayoutGroupComponent);
        CHECK_COMP(UILabel, UILabelComponent);
        CHECK_COMP(UIButton, UIButtonComponent);
        CHECK_COMP(UITextInput, UITextInputComponent);
        CHECK_COMP(UICheckbox, UICheckboxComponent);
        CHECK_COMP(UIDropdown, UIDropdownComponent);
        CHECK_COMP(UITabs, UITabsComponent);
        CHECK_COMP(UISlider, UISliderComponent);
        CHECK_COMP(UIProgressBar, UIProgressBarComponent);
        CHECK_COMP(UIStyle, UIStyleComponent);
        CHECK_COMP(RenderTexture, RenderTextureComponent);
        CHECK_COMP(Decal, DecalComponent);

#undef CHECK_COMP

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

        auto& registry = scene::EntityRegistry::getRegistry();
        data.isEffectivelyActive = scene::Entity::isEffectivelyActive(registry, entity);

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
