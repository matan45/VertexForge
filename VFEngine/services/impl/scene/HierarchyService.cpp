#include "HierarchyService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "components/ComponentClone.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "resource/AssetLifecycleHelpers.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../data/ScriptTypes.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/scripting/ScriptingEvents.hpp"
#include <functional>

namespace services
{
    HierarchyService::HierarchyService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
    {
    }

    void HierarchyService::registerEventHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::scene::CreateEntityCommand>(
            [this](const events::scene::CreateEntityCommand& cmd)
            {
                return createEntity(cmd.name, cmd.parent);
            });

        dispatcher.registerCommandHandler<events::scene::DeleteEntityCommand>(
            [this](const events::scene::DeleteEntityCommand& cmd)
            {
                return deleteEntity(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::DuplicateEntityCommand>(
            [this](const events::scene::DuplicateEntityCommand& cmd)
            {
                return duplicateEntity(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::ReparentEntityCommand>(
            [this](const events::scene::ReparentEntityCommand& cmd)
            {
                return reparentEntity(cmd.entity, cmd.newParent);
            });
    }

    void HierarchyService::collectEntityAndDescendants(scene::Entity& entity, std::vector<EntityHandle>& outHandles) const
    {
        outHandles.push_back(internal::toHandle(entity.getHandle()));
        for (auto& child : entity.getChildren())
        {
            collectEntityAndDescendants(child, outHandles);
        }
    }

    EntityHandle HierarchyService::createEntity(const std::string& name, std::optional<EntityHandle> parent)
    {
        scene::Entity newEntity(name);

        if (parent.has_value() && parent->isValid())
        {
            auto parentEntt = internal::fromHandle(*parent);
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.valid(parentEntt))
            {
                scene::Entity parentEntity(parentEntt);
                sceneGraph->addChild(parentEntity, newEntity);
            }
            else
            {
                // Parent was destroyed, fall back to root
                sceneGraph->addChild(sceneGraph->GetRoot(), newEntity);
            }
        }
        else
        {
            sceneGraph->addChild(sceneGraph->GetRoot(), newEntity);
        }

        auto handle = internal::toHandle(newEntity.getHandle());

        events::scene::EntityCreatedNotification notification;
        notification.entity = handle;
        notification.name = name;
        notification.parent = parent;
        events::EventDispatcher::instance().publish(notification);

        return handle;
    }

    bool HierarchyService::deleteEntity(EntityHandle entity, bool deleteChildren)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        std::vector<EntityHandle> entitiesToDelete;
        collectEntityAndDescendants(sceneEntity, entitiesToDelete);

        auto& dispatcher = events::EventDispatcher::instance();
        for (const auto& handle : entitiesToDelete)
        {
            auto enttEntity = internal::fromHandle(handle);
            if (registry.valid(enttEntity) && registry.all_of<components::ScriptComponent>(enttEntity))
            {
                const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
                for (const auto& entry : scriptComp.scripts)
                {
                    events::scripting::DetachScriptCommand cmd;
                    cmd.entity = handle;
                    cmd.scriptPath = entry.scriptRef.resolve();
                    dispatcher.execute(cmd);
                }
            }
        }

        // Clear global selection if the deleted entity (or any descendant) is currently selected
        auto currentSelection = dispatcher.query(events::scene::GetSelectedEntityQuery{});
        if (currentSelection.has_value())
        {
            for (const auto& handle : entitiesToDelete)
            {
                if (handle.id == currentSelection->id)
                {
                    events::scene::SelectEntityCommand clearCmd;
                    clearCmd.entity = std::nullopt;
                    dispatcher.execute(clearCmd);
                    break;
                }
            }
        }

        // Publish before removeEntity so subscribers can still read components.
        // Publish for every entity in the subtree (not just the root) so that
        // asset lifecycle and other listeners can release per-entity resources.
        for (const auto& handle : entitiesToDelete)
        {
            auto enttEntity = internal::fromHandle(handle);
            if (registry.valid(enttEntity))
            {
                events::scene::EntityDeletedNotification notification;
                notification.entity = handle;
                dispatcher.publish(notification);
            }
        }

        sceneGraph->removeEntity(sceneEntity);

        return true;
    }

    EntityHandle HierarchyService::duplicateEntity(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return EntityHandle::invalid();
        }

        scene::Entity& root = sceneGraph->GetRoot();
        if (internal::fromHandle(entity) == root.getHandle())
        {
            vfLogWarning("Cannot duplicate root entity");
            return EntityHandle::invalid();
        }

        scene::Entity original(internal::fromHandle(entity));
        auto& dispatcher = events::EventDispatcher::instance();

        std::function<EntityHandle(scene::Entity&, std::optional<EntityHandle>)> duplicateRecursive =
            [&](scene::Entity& orig, std::optional<EntityHandle> parentHandle) -> EntityHandle
        {
            std::string newName = orig.getName() + " (Copy)";
            auto newHandle = createEntity(newName, parentHandle);
            scene::Entity newEntity(internal::fromHandle(newHandle));

            if (orig.hasComponent<components::TransformComponent>())
            {
                auto& origTransform = orig.getComponent<components::TransformComponent>();
                auto& newTransform = newEntity.getComponent<components::TransformComponent>();
                newTransform.position = origTransform.position;
                newTransform.rotation = origTransform.rotation;
                newTransform.scale = origTransform.scale;
                newTransform.isStatic = origTransform.isStatic;
                newTransform.isDirty = true;
            }

            // Deep-copy every optional component (UI + non-UI), resetting runtime-only state.
            // Picks up new components automatically via components::OptionalComponents.
            // ScriptComponent is excluded here and reattached below so script instances spawn.
            components::cloneOptionalComponents(orig, newEntity);

            // Balance the per-entity releaseEntityAssets() that fires on delete —
            // the clone holds its own refs to the same mesh/material/audio assets.
            resource::acquireEntityAssets(newEntity, resource::AssetLifecycleManager::instance());

            // MeshComponent registration side-effect: notify so the duplicate registers with the
            // GPU-driven renderer (the component data itself was already copied above).
            if (newEntity.hasComponent<components::MeshComponent>())
            {
                auto& newMesh = newEntity.getComponent<components::MeshComponent>();
                if (newMesh.meshRef.isValid())
                {
                    events::scene::MeshDataChangedNotification meshNotif;
                    meshNotif.entity = newHandle;
                    meshNotif.meshPath = newMesh.meshRef.resolve();
                    meshNotif.animatorPath = newMesh.animatorRef.resolve();
                    dispatcher.publish(meshNotif);
                }
            }

            // ScriptComponent: reattach via the scripting system (instantiates runtime script
            // instances) rather than value-copying the component.
            if (orig.hasComponent<components::ScriptComponent>())
            {
                auto& origScripts = orig.getComponent<components::ScriptComponent>();
                for (const auto& entry : origScripts.scripts)
                {
                    events::scripting::AttachScriptCommand cmd;
                    cmd.entity = newHandle;
                    cmd.data.scriptPath = entry.scriptRef.resolve();
                    cmd.data.enabled = entry.enabled;
                    dispatcher.execute(cmd);
                }
            }

            for (auto& child : orig.getChildren())
            {
                duplicateRecursive(child, newHandle);
            }

            return newHandle;
        };

        std::optional<EntityHandle> parent;
        if (original.hasComponent<components::ParentComponent>())
        {
            parent = internal::toHandle(original.getComponent<components::ParentComponent>().parent);
        }

        auto duplicatedHandle = duplicateRecursive(original, parent);

        events::scene::EntityDuplicatedNotification notification;
        notification.originalEntity = entity;
        notification.duplicatedEntity = duplicatedHandle;
        dispatcher.publish(notification);

        return duplicatedHandle;
    }

    bool HierarchyService::reparentEntity(EntityHandle entity, EntityHandle newParent)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry) ||
            !internal::isValidHandle(newParent, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        scene::Entity newParentEntity(internal::fromHandle(newParent));

        std::optional<EntityHandle> oldParent;
        if (sceneEntity.hasComponent<components::ParentComponent>())
        {
            oldParent = internal::toHandle(sceneEntity.getComponent<components::ParentComponent>().parent);
        }

        sceneGraph->moveEntity(sceneEntity, newParentEntity);

        events::scene::EntityReparentedNotification notification;
        notification.entity = entity;
        notification.oldParent = oldParent;
        notification.newParent = newParent;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    bool HierarchyService::moveEntity(EntityHandle entity, EntityHandle targetParent, int insertIndex)
    {
        return reparentEntity(entity, targetParent);
    }

    EntityHandle HierarchyService::getRoot() const
    {
        return internal::toHandle(sceneGraph->GetRoot().getHandle());
    }

    std::vector<EntityHandle> HierarchyService::getChildren(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<EntityHandle> children;

        if (!internal::isValidHandle(entity, registry))
        {
            return children;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ChildrenComponent>())
        {
            return children;
        }

        auto& childrenComp = sceneEntity.getComponent<components::ChildrenComponent>();
        for (auto child : childrenComp.children)
        {
            children.push_back(internal::toHandle(child));
        }

        return children;
    }
}
