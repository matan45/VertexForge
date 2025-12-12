#include "SceneServiceImpl.hpp"
#include "../../utilities/scene/SceneGraphSystem.hpp"
#include "../../utilities/scene/Entity.hpp"
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"
#include "../../utilities/serialization/SceneSerialization.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/RenderEvents.hpp"
#include "print/EditorLogger.hpp"

namespace services {

    SceneServiceImpl::SceneServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    EntityHandle SceneServiceImpl::createEntity(const std::string& name,
        std::optional<EntityHandle> parent) {

        scene::Entity newEntity(name);

        // Add to parent or root
        if (parent.has_value() && parent->isValid()) {
            scene::Entity parentEntity(internal::fromHandle(*parent));
            sceneGraph->addChild(parentEntity, newEntity);
        }
        else {
            sceneGraph->addChild(sceneGraph->GetRoot(), newEntity);
        }

        auto handle = internal::toHandle(newEntity.getHandle());

        // Publish notification
        events::scene::EntityCreatedNotification notification;
        notification.entity = handle;
        notification.name = name;
        notification.parent = parent;
        events::EventDispatcher::instance().publish(notification);

        return handle;
    }

    bool SceneServiceImpl::deleteEntity(EntityHandle entity, bool deleteChildren) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        sceneGraph->removeEntity(sceneEntity);

        // Publish notification
        events::scene::EntityDeletedNotification notification;
        notification.entity = entity;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    EntityHandle SceneServiceImpl::duplicateEntity(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return EntityHandle::invalid();
        }

        scene::Entity original(internal::fromHandle(entity));
        std::string newName = original.getName() + " (Copy)";

        // Get parent
        std::optional<EntityHandle> parent;
        if (original.hasComponent<components::ParentComponent>()) {
            parent = internal::toHandle(original.getComponent<components::ParentComponent>().parent);
        }

        // Create duplicate
        auto newHandle = createEntity(newName, parent);

        // Copy transform if exists
        if (original.hasComponent<components::TransformComponent>()) {
            auto& origTransform = original.getComponent<components::TransformComponent>();
            scene::Entity newEntity(internal::fromHandle(newHandle));
            auto& newTransform = newEntity.getComponent<components::TransformComponent>();
            newTransform.position = origTransform.position;
            newTransform.rotation = origTransform.rotation;
            newTransform.scale = origTransform.scale;
            newTransform.isDirty = true;
        }

        return newHandle;
    }

    bool SceneServiceImpl::reparentEntity(EntityHandle entity, EntityHandle newParent) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry) ||
            !internal::isValidHandle(newParent, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        scene::Entity newParentEntity(internal::fromHandle(newParent));

        // Get old parent for notification
        std::optional<EntityHandle> oldParent;
        if (sceneEntity.hasComponent<components::ParentComponent>()) {
            oldParent = internal::toHandle(sceneEntity.getComponent<components::ParentComponent>().parent);
        }

        sceneGraph->moveEntity(sceneEntity, newParentEntity);

        // Publish notification
        events::scene::EntityReparentedNotification notification;
        notification.entity = entity;
        notification.oldParent = oldParent;
        notification.newParent = newParent;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    bool SceneServiceImpl::moveEntity(EntityHandle entity, EntityHandle targetParent, int insertIndex) {
        // For now, just reparent - index handling would require more complex logic
        return reparentEntity(entity, targetParent);
    }

    EntityHandle SceneServiceImpl::getRoot() const {
        return internal::toHandle(sceneGraph->GetRoot().getHandle());
    }

    std::optional<EntityData> SceneServiceImpl::getEntity(EntityHandle handle) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(handle, registry)) {
            return std::nullopt;
        }

        return buildEntityData(internal::fromHandle(handle));
    }

    std::optional<EntityHandle> SceneServiceImpl::findEntityByName(const std::string& name) const {
        auto entities = sceneGraph->findAllEntitiesByName(name);
        if (entities.empty()) {
            return std::nullopt;
        }
        return internal::toHandle(entities[0].getHandle());
    }

    std::vector<EntityHandle> SceneServiceImpl::findEntitiesByName(const std::string& name) const {
        auto entities = sceneGraph->findAllEntitiesByName(name);
        std::vector<EntityHandle> handles;
        handles.reserve(entities.size());
        for (auto& entity : entities) {
            handles.push_back(internal::toHandle(entity.getHandle()));
        }
        return handles;
    }

    std::vector<EntityHandle> SceneServiceImpl::getEntitiesWithComponent(ComponentTypeId type) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<EntityHandle> handles;

        switch (type) {
        case ComponentTypeId::Camera: {
            auto view = registry.view<components::CameraComponent>();
            for (auto entity : view) {
                handles.push_back(internal::toHandle(entity));
            }
            break;
        }
        case ComponentTypeId::Transform: {
            auto view = registry.view<components::TransformComponent>();
            for (auto entity : view) {
                handles.push_back(internal::toHandle(entity));
            }
            break;
        }
        case ComponentTypeId::IBL: {
            auto view = registry.view<components::IBLComponent>();
            for (auto entity : view) {
                handles.push_back(internal::toHandle(entity));
            }
            break;
        }
        case ComponentTypeId::Mesh: {
            auto view = registry.view<components::MeshComponent>();
            for (auto entity : view) {
                handles.push_back(internal::toHandle(entity));
            }
            break;
        }
        default:
            break;
        }

        return handles;
    }

    SceneHierarchyData SceneServiceImpl::getSceneHierarchy() const {
        SceneHierarchyData data;
        data.root = getRoot();

        // Collect all entities recursively
        collectHierarchy(internal::fromHandle(data.root), data.entities);

        return data;
    }

    void SceneServiceImpl::setTransform(EntityHandle entity, const TransformData& transform) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::TransformComponent>()) {
            auto& comp = sceneEntity.getComponent<components::TransformComponent>();
            comp.position = transform.position;
            comp.rotation = transform.rotation;
            comp.scale = transform.scale;
            comp.isDirty = true;

            // Publish notification
            events::scene::TransformChangedNotification notification;
            notification.entity = entity;
            notification.newTransform = transform;
            events::EventDispatcher::instance().publish(notification);
        }
    }

    std::optional<TransformData> SceneServiceImpl::getTransform(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::TransformComponent>()) {
            return std::nullopt;
        }

        auto& comp = sceneEntity.getComponent<components::TransformComponent>();
        return TransformData{ comp.position, comp.rotation, comp.scale };
    }

    std::optional<TransformData> SceneServiceImpl::getWorldTransform(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::WorldTransformComponent>()) {
            return getTransform(entity);  // Fall back to local transform
        }

        // WorldTransformComponent only has matrix, so we'd need to decompose
        // For now, return local transform
        return getTransform(entity);
    }

    bool SceneServiceImpl::hasComponent(EntityHandle entity, ComponentTypeId type) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        auto enttEntity = internal::fromHandle(entity);

        switch (type) {
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
        default:
            return false;
        }
    }

    std::vector<ComponentTypeId> SceneServiceImpl::getComponentTypes(EntityHandle entity) const {
        std::vector<ComponentTypeId> types;
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
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

        return types;
    }

    std::optional<CameraData> SceneServiceImpl::getCameraData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::CameraComponent>()) {
            return std::nullopt;
        }

        auto& comp = sceneEntity.getComponent<components::CameraComponent>();
        CameraData data;
        data.fieldOfView = comp.fieldOfView;
        data.nearPlane = comp.nearPlane;
        data.farPlane = comp.farPlane;
        data.aspectRatio = comp.aspectRatio;
        data.isPerspective = comp.isPerspective;
        data.orthoSize = comp.orthoSize;

        return data;
    }

    bool SceneServiceImpl::setCameraData(EntityHandle entity, const CameraData& camera) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::CameraComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::CameraComponent>();
        comp.fieldOfView = camera.fieldOfView;
        comp.nearPlane = camera.nearPlane;
        comp.farPlane = camera.farPlane;
        comp.aspectRatio = camera.aspectRatio;
        comp.isPerspective = camera.isPerspective;
        comp.orthoSize = camera.orthoSize;
        comp.updateProjectionMatrix();

        return true;
    }

    std::optional<EntityHandle> SceneServiceImpl::getPrimaryCamera() const {
        auto cameras = getEntitiesWithComponent(ComponentTypeId::Camera);
        if (cameras.empty()) {
            return std::nullopt;
        }
        return cameras[0];
    }

    bool SceneServiceImpl::addCameraComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        // Ensure entity has TransformComponent (required for camera updates)
        if (!sceneEntity.hasComponent<components::TransformComponent>()) {
            sceneEntity.addComponent<components::TransformComponent>();
        }

        if (!sceneEntity.hasComponent<components::CameraComponent>()) {
            sceneEntity.addComponent<components::CameraComponent>();
            return true;
        }

        return false;  // Already has camera
    }

    bool SceneServiceImpl::removeCameraComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::CameraComponent>()) {
            sceneEntity.removeComponent<components::CameraComponent>();
            return true;
        }

        return false;
    }

    std::optional<IBLData> SceneServiceImpl::getIBLData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::IBLComponent>()) {
            return std::nullopt;
        }

        auto& comp = sceneEntity.getComponent<components::IBLComponent>();
        IBLData data;
        data.fileName = comp.fileName;

        return data;
    }

    bool SceneServiceImpl::setIBLData(EntityHandle entity, const IBLData& ibl) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::IBLComponent>()) {
            auto& comp = sceneEntity.getComponent<components::IBLComponent>();
            comp.fileName = ibl.fileName;
        }
        else {
            sceneEntity.addComponent<components::IBLComponent>(ibl.fileName);
        }

        return true;
    }

    bool SceneServiceImpl::removeIBLComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::IBLComponent>()) {
            sceneEntity.removeComponent<components::IBLComponent>();
            return true;
        }

        return false;
    }

    std::optional<MeshData> SceneServiceImpl::getMeshData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MeshComponent>()) {
            return std::nullopt;
        }

        auto& comp = sceneEntity.getComponent<components::MeshComponent>();
        MeshData data;
        data.meshPath = comp.meshPath;

        return data;
    }

    bool SceneServiceImpl::setMeshData(EntityHandle entity, const MeshData& mesh) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::MeshComponent>()) {
            auto& comp = sceneEntity.getComponent<components::MeshComponent>();
            comp.meshPath = mesh.meshPath;
        }
        else {
            auto& comp = sceneEntity.addComponent<components::MeshComponent>();
            comp.meshPath = mesh.meshPath;
        }

        return true;
    }

    bool SceneServiceImpl::addMeshComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MeshComponent>()) {
            sceneEntity.addComponent<components::MeshComponent>();
            return true;
        }

        return false;  // Already has mesh component
    }

    bool SceneServiceImpl::removeMeshComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::MeshComponent>()) {
            sceneEntity.removeComponent<components::MeshComponent>();
            return true;
        }

        return false;
    }

    bool SceneServiceImpl::hasMeshComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::MeshComponent>();
    }

    std::vector<EntityHandle> SceneServiceImpl::getChildren(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<EntityHandle> children;

        if (!internal::isValidHandle(entity, registry)) {
            return children;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ChildrenComponent>()) {
            return children;
        }

        auto& childrenComp = sceneEntity.getComponent<components::ChildrenComponent>();
        for (auto child : childrenComp.children) {
            children.push_back(internal::toHandle(child));
        }

        return children;
    }

    void SceneServiceImpl::setSelectedEntity(std::optional<EntityHandle> entity) {
        selectedEntity = entity;

        // Publish notification
        events::scene::EntitySelectedNotification notification;
        notification.entity = entity;
        events::EventDispatcher::instance().publish(notification);
    }

    std::optional<EntityHandle> SceneServiceImpl::getSelectedEntity() const {
        return selectedEntity;
    }

    std::string SceneServiceImpl::getEntityName(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return "";
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.getName();
    }

    void SceneServiceImpl::setEntityName(EntityHandle entity, const std::string& name) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        sceneEntity.setName(name);
    }

    bool SceneServiceImpl::newScene() {
        if (!sceneGraph) {
            vfLogError("SceneGraph is null, cannot create new scene.");
            return false;
        }

        auto& dispatcher = events::EventDispatcher::instance();
        
        events::render::RemoveIBLCommand removeIblCmd;
        dispatcher.execute(removeIblCmd);
        
        sceneGraph->clearScene();
        
        selectedEntity = std::nullopt;
        
        events::scene::SceneClearedNotification notification;
        dispatcher.publish(notification);

        vfLogInfo("New scene created.");
        return true;
    }

    bool SceneServiceImpl::saveScene(const std::string& filePath) {
        if (!sceneGraph) {
            vfLogError("SceneGraph is null, cannot save scene.");
            return false;
        }

        if (filePath.empty()) {
            vfLogError("File path is empty, cannot save scene.");
            return false;
        }

        return serialization::SceneSerialization::saveScene(*sceneGraph, filePath);
    }

    bool SceneServiceImpl::loadScene(const std::string& filePath) {
        if (!sceneGraph) {
            vfLogError("SceneGraph is null, cannot load scene.");
            return false;
        }

        if (filePath.empty()) {
            vfLogError("File path is empty, cannot load scene.");
            return false;
        }

        auto& dispatcher = events::EventDispatcher::instance();
        
        events::render::RemoveIBLCommand removeIblCmd;
        dispatcher.execute(removeIblCmd);

        // Clear selection
        selectedEntity = std::nullopt;
        
        bool success = serialization::SceneSerialization::loadSceneInto(filePath, *sceneGraph);

        if (success) {
            // IBL is a scene-level property, always attached to root entity only.
            // Non-root entities should not have IBL components (enforced by editor UI).
            scene::Entity& root = sceneGraph->GetRoot();
            if (root.hasComponent<components::IBLComponent>()) {
                const auto& ibl = root.getComponent<components::IBLComponent>();
                if (!ibl.fileName.empty()) {
                    events::render::SetIBLCommand setIblCmd;
                    setIblCmd.hdrPath = ibl.fileName;
                    dispatcher.execute(setIblCmd);
                }
            }

            // Publish scene loaded notification
            events::scene::SceneLoadedNotification notification;
            notification.scenePath = filePath;
            dispatcher.publish(notification);
        }

        return success;
    }

    EntityData SceneServiceImpl::buildEntityData(entt::entity entity) const {
        scene::Entity sceneEntity(entity);

        EntityData data;
        data.handle = internal::toHandle(entity);
        data.name = sceneEntity.getName();

        // Parent
        if (sceneEntity.hasComponent<components::ParentComponent>()) {
            data.parent = internal::toHandle(sceneEntity.getComponent<components::ParentComponent>().parent);
        }

        // Children
        if (sceneEntity.hasComponent<components::ChildrenComponent>()) {
            auto& children = sceneEntity.getComponent<components::ChildrenComponent>().children;
            for (auto child : children) {
                data.children.push_back(internal::toHandle(child));
            }
        }

        // Transform
        if (sceneEntity.hasComponent<components::TransformComponent>()) {
            auto& transform = sceneEntity.getComponent<components::TransformComponent>();
            data.localTransform = TransformData{ transform.position, transform.rotation, transform.scale };
        }

        // World transform
        if (sceneEntity.hasComponent<components::WorldTransformComponent>()) {
            // For now, just copy local transform
            data.worldTransform = data.localTransform;
        }

        // Components list
        data.components = getComponentTypes(data.handle);

        return data;
    }

    void SceneServiceImpl::collectHierarchy(entt::entity entity, std::vector<EntityData>& entities) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(entity)) return;

        entities.push_back(buildEntityData(entity));

        scene::Entity sceneEntity(entity);
        if (sceneEntity.hasComponent<components::ChildrenComponent>()) {
            auto& children = sceneEntity.getComponent<components::ChildrenComponent>().children;
            for (auto child : children) {
                collectHierarchy(child, entities);
            }
        }
    }

    void SceneServiceImpl::registerEventHandlers() {
        auto& dispatcher = events::EventDispatcher::instance();

        // Command handlers
        dispatcher.registerCommandHandler<events::scene::CreateEntityCommand>(
            [this](const events::scene::CreateEntityCommand& cmd) {
                return createEntity(cmd.name, cmd.parent);
            });

        dispatcher.registerCommandHandler<events::scene::DeleteEntityCommand>(
            [this](const events::scene::DeleteEntityCommand& cmd) {
                return deleteEntity(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::ReparentEntityCommand>(
            [this](const events::scene::ReparentEntityCommand& cmd) {
                return reparentEntity(cmd.entity, cmd.newParent);
            });

        dispatcher.registerCommandHandler<events::scene::SetTransformCommand>(
            [this](const events::scene::SetTransformCommand& cmd) {
                setTransform(cmd.entity, cmd.transform);
            });

        dispatcher.registerCommandHandler<events::scene::SetEntityNameCommand>(
            [this](const events::scene::SetEntityNameCommand& cmd) {
                setEntityName(cmd.entity, cmd.newName);
            });

        dispatcher.registerCommandHandler<events::scene::SelectEntityCommand>(
            [this](const events::scene::SelectEntityCommand& cmd) {
                setSelectedEntity(cmd.entity);
            });

        // Query handlers
        dispatcher.registerQueryHandler<events::scene::GetEntityQuery>(
            [this](const events::scene::GetEntityQuery& query) {
                return getEntity(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetTransformQuery>(
            [this](const events::scene::GetTransformQuery& query) {
                return getTransform(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetSceneHierarchyQuery>(
            [this](const events::scene::GetSceneHierarchyQuery&) {
                return getSceneHierarchy();
            });

        dispatcher.registerQueryHandler<events::scene::GetSelectedEntityQuery>(
            [this](const events::scene::GetSelectedEntityQuery&) {
                return getSelectedEntity();
            });

        dispatcher.registerQueryHandler<events::scene::FindEntitiesByNameQuery>(
            [this](const events::scene::FindEntitiesByNameQuery& query) {
                return findEntitiesByName(query.name);
            });

        dispatcher.registerQueryHandler<events::scene::GetPrimaryCameraQuery>(
            [this](const events::scene::GetPrimaryCameraQuery&) {
                return getPrimaryCamera();
            });

        dispatcher.registerQueryHandler<events::scene::GetRootEntityQuery>(
            [this](const events::scene::GetRootEntityQuery&) {
                return getRoot();
            });

        dispatcher.registerCommandHandler<events::scene::SetIBLDataCommand>(
            [this](const events::scene::SetIBLDataCommand& cmd) {
                return setIBLData(cmd.entity, cmd.iblData);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveIBLComponentCommand>(
            [this](const events::scene::RemoveIBLComponentCommand& cmd) {
                return removeIBLComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::AddCameraComponentCommand>(
            [this](const events::scene::AddCameraComponentCommand& cmd) {
                return addCameraComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveCameraComponentCommand>(
            [this](const events::scene::RemoveCameraComponentCommand& cmd) {
                return removeCameraComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetCameraDataCommand>(
            [this](const events::scene::SetCameraDataCommand& cmd) {
                return setCameraData(cmd.entity, cmd.cameraData);
            });

        dispatcher.registerQueryHandler<events::scene::GetCameraDataQuery>(
            [this](const events::scene::GetCameraDataQuery& query) {
                return getCameraData(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::HasCameraComponentQuery>(
            [this](const events::scene::HasCameraComponentQuery& query) {
                return hasComponent(query.entity, ComponentTypeId::Camera);
            });

        dispatcher.registerQueryHandler<events::scene::HasIBLComponentQuery>(
            [this](const events::scene::HasIBLComponentQuery& query) {
                return hasComponent(query.entity, ComponentTypeId::IBL);
            });

        dispatcher.registerQueryHandler<events::scene::GetIBLDataQuery>(
            [this](const events::scene::GetIBLDataQuery& query) {
                return getIBLData(query.entity);
            });

        dispatcher.registerCommandHandler<events::scene::NewSceneCommand>(
            [this](const events::scene::NewSceneCommand&) {
                return newScene();
            });

        dispatcher.registerCommandHandler<events::scene::SaveSceneCommand>(
            [this](const events::scene::SaveSceneCommand& cmd) {
                return saveScene(cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::scene::LoadSceneCommand>(
            [this](const events::scene::LoadSceneCommand& cmd) {
                return loadScene(cmd.filePath);
            });

        // Mesh Component handlers
        dispatcher.registerCommandHandler<events::scene::AddMeshComponentCommand>(
            [this](const events::scene::AddMeshComponentCommand& cmd) {
                return addMeshComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveMeshComponentCommand>(
            [this](const events::scene::RemoveMeshComponentCommand& cmd) {
                return removeMeshComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetMeshDataCommand>(
            [this](const events::scene::SetMeshDataCommand& cmd) {
                return setMeshData(cmd.entity, cmd.meshData);
            });

        dispatcher.registerQueryHandler<events::scene::HasMeshComponentQuery>(
            [this](const events::scene::HasMeshComponentQuery& query) {
                return hasMeshComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetMeshDataQuery>(
            [this](const events::scene::GetMeshDataQuery& query) {
                return getMeshData(query.entity);
            });
    }

}
