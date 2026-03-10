#include "HierarchyService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
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
                    cmd.scriptPath = entry.scriptPath;
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

            if (orig.hasComponent<components::CameraComponent>())
            {
                auto& origCamera = orig.getComponent<components::CameraComponent>();
                auto& newCamera = newEntity.addComponent<components::CameraComponent>();
                newCamera.isPerspective = origCamera.isPerspective;
                newCamera.isPrimary = false;
                newCamera.showFrustum = origCamera.showFrustum;
                newCamera.fieldOfView = origCamera.fieldOfView;
                newCamera.orthoSize = origCamera.orthoSize;
                newCamera.nearPlane = origCamera.nearPlane;
                newCamera.farPlane = origCamera.farPlane;
                newCamera.aspectRatio = origCamera.aspectRatio;
                newCamera.enableOcclusionCulling = origCamera.enableOcclusionCulling;
                newCamera.updateProjectionMatrix();
            }

            if (orig.hasComponent<components::IBLComponent>())
            {
                auto& origIBL = orig.getComponent<components::IBLComponent>();
                auto& newIBL = newEntity.addComponent<components::IBLComponent>();
                newIBL.fileName = origIBL.fileName;
            }

            if (orig.hasComponent<components::MeshComponent>())
            {
                auto& origMesh = orig.getComponent<components::MeshComponent>();
                auto& newMesh = newEntity.addComponent<components::MeshComponent>();
                newMesh.meshPath = origMesh.meshPath;
                newMesh.animatorPath = origMesh.animatorPath;
                newMesh.showBoundingBox = origMesh.showBoundingBox;

                if (!newMesh.meshPath.empty())
                {
                    events::scene::MeshDataChangedNotification meshNotif;
                    meshNotif.entity = newHandle;
                    meshNotif.meshPath = newMesh.meshPath;
                    meshNotif.animatorPath = newMesh.animatorPath;
                    dispatcher.publish(meshNotif);
                }
            }

            if (orig.hasComponent<components::MaterialComponent>())
            {
                auto& origMat = orig.getComponent<components::MaterialComponent>();
                auto& newMat = newEntity.addComponent<components::MaterialComponent>();
                newMat.defaultMaterial = origMat.defaultMaterial;
                newMat.subMeshMaterials = origMat.subMeshMaterials;
                newMat.parameterOverrides = origMat.parameterOverrides;
            }

            if (orig.hasComponent<components::AudioSource2DComponent>())
            {
                auto& origAudio = orig.getComponent<components::AudioSource2DComponent>();
                auto& newAudio = newEntity.addComponent<components::AudioSource2DComponent>();
                newAudio.audioFilePath = origAudio.audioFilePath;
                newAudio.volume = origAudio.volume;
                newAudio.pitch = origAudio.pitch;
                newAudio.loop = origAudio.loop;
            }

            if (orig.hasComponent<components::AudioSource3DComponent>())
            {
                auto& origAudio = orig.getComponent<components::AudioSource3DComponent>();
                auto& newAudio = newEntity.addComponent<components::AudioSource3DComponent>();
                newAudio.audioFilePath = origAudio.audioFilePath;
                newAudio.volume = origAudio.volume;
                newAudio.pitch = origAudio.pitch;
                newAudio.loop = origAudio.loop;
                newAudio.minDistance = origAudio.minDistance;
                newAudio.maxDistance = origAudio.maxDistance;
                newAudio.showDebugSpheres = origAudio.showDebugSpheres;
            }

            if (orig.hasComponent<components::ScriptComponent>())
            {
                auto& origScripts = orig.getComponent<components::ScriptComponent>();
                for (const auto& entry : origScripts.scripts)
                {
                    events::scripting::AttachScriptCommand cmd;
                    cmd.entity = newHandle;
                    cmd.data.scriptPath = entry.scriptPath;
                    cmd.data.enabled = entry.enabled;
                    dispatcher.execute(cmd);
                }
            }

            if (orig.hasComponent<components::ColliderComponent>())
            {
                auto& origCollider = orig.getComponent<components::ColliderComponent>();
                auto& newCollider = newEntity.addComponent<components::ColliderComponent>();
                newCollider.shape = origCollider.shape;
                newCollider.size = origCollider.size;
                newCollider.height = origCollider.height;
                newCollider.offset = origCollider.offset;
                newCollider.meshPath = origCollider.meshPath;
                newCollider.isTrigger = origCollider.isTrigger;
                newCollider.collisionLayer = origCollider.collisionLayer;
                newCollider.friction = origCollider.friction;
                newCollider.restitution = origCollider.restitution;
            }

            if (orig.hasComponent<components::RigidBodyComponent>())
            {
                auto& origRB = orig.getComponent<components::RigidBodyComponent>();
                auto& newRB = newEntity.addComponent<components::RigidBodyComponent>();
                newRB.type = origRB.type;
                newRB.mass = origRB.mass;
                newRB.linearDamping = origRB.linearDamping;
                newRB.angularDamping = origRB.angularDamping;
                newRB.freezePositionX = origRB.freezePositionX;
                newRB.freezePositionY = origRB.freezePositionY;
                newRB.freezePositionZ = origRB.freezePositionZ;
                newRB.freezeRotationX = origRB.freezeRotationX;
                newRB.freezeRotationY = origRB.freezeRotationY;
                newRB.freezeRotationZ = origRB.freezeRotationZ;
            }

            if (orig.hasComponent<components::DirectionalLightComponent>())
            {
                auto& origLight = orig.getComponent<components::DirectionalLightComponent>();
                auto& newLight = newEntity.addComponent<components::DirectionalLightComponent>();
                newLight.color = origLight.color;
                newLight.intensity = origLight.intensity;
                newLight.showGizmo = origLight.showGizmo;
            }

            if (orig.hasComponent<components::PointLightComponent>())
            {
                auto& origLight = orig.getComponent<components::PointLightComponent>();
                auto& newLight = newEntity.addComponent<components::PointLightComponent>();
                newLight.color = origLight.color;
                newLight.intensity = origLight.intensity;
                newLight.radius = origLight.radius;
                newLight.showGizmo = origLight.showGizmo;
            }

            if (orig.hasComponent<components::SpotLightComponent>())
            {
                auto& origLight = orig.getComponent<components::SpotLightComponent>();
                auto& newLight = newEntity.addComponent<components::SpotLightComponent>();
                newLight.color = origLight.color;
                newLight.intensity = origLight.intensity;
                newLight.innerAngle = origLight.innerAngle;
                newLight.outerAngle = origLight.outerAngle;
                newLight.range = origLight.range;
                newLight.showGizmo = origLight.showGizmo;
            }

            if (orig.hasComponent<components::VFXComponent>())
            {
                auto& origVFX = orig.getComponent<components::VFXComponent>();
                auto& newVFX = newEntity.addComponent<components::VFXComponent>();
                newVFX.vfxPath = origVFX.vfxPath;
                newVFX.autoPlay = origVFX.autoPlay;
                newVFX.loop = origVFX.loop;
            }

            if (orig.hasComponent<components::AnimatorComponent>())
            {
                auto& origAnim = orig.getComponent<components::AnimatorComponent>();
                auto& newAnim = newEntity.addComponent<components::AnimatorComponent>();
                newAnim.animatorPath = origAnim.animatorPath;
            }

            if (orig.hasComponent<components::BillboardComponent>())
            {
                auto& origBillboard = orig.getComponent<components::BillboardComponent>();
                auto& newBillboard = newEntity.addComponent<components::BillboardComponent>();
                newBillboard.iconType = origBillboard.iconType;
                newBillboard.atlasIndex = origBillboard.atlasIndex;
                newBillboard.sizeMode = origBillboard.sizeMode;
                newBillboard.size = origBillboard.size;
                newBillboard.colorTint = origBillboard.colorTint;
                newBillboard.editorOnly = origBillboard.editorOnly;
                newBillboard.selectable = origBillboard.selectable;
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
