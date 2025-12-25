#include "SceneServiceImpl.hpp"
#include "../../utilities/scene/SceneGraphSystem.hpp"
#include "../../utilities/scene/Entity.hpp"
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"
#include "../../utilities/serialization/SceneSerialization.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/SceneEvents.hpp"
#include "../events/RenderEvents.hpp"
#include "../events/MaterialEvents.hpp"
#include "../events/AudioEvents.hpp"
#include "print/EditorLogger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

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
        case ComponentTypeId::AudioSource2D: {
            auto view = registry.view<components::AudioSource2DComponent>();
            for (auto entity : view) {
                handles.push_back(internal::toHandle(entity));
            }
            break;
        }
        case ComponentTypeId::AudioSource3D: {
            auto view = registry.view<components::AudioSource3DComponent>();
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
        case ComponentTypeId::AudioSource2D:
            return registry.all_of<components::AudioSource2DComponent>(enttEntity);
        case ComponentTypeId::AudioSource3D:
            return registry.all_of<components::AudioSource3DComponent>(enttEntity);
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
        if (registry.all_of<components::AudioSource2DComponent>(enttEntity))
            types.push_back(ComponentTypeId::AudioSource2D);
        if (registry.all_of<components::AudioSource3DComponent>(enttEntity))
            types.push_back(ComponentTypeId::AudioSource3D);

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
        data.isPrimary = comp.isPrimary;
        data.showFrustum = comp.showFrustum;
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
        comp.showFrustum = camera.showFrustum;
        comp.orthoSize = camera.orthoSize;

        // If setting this camera as primary, clear isPrimary from all other cameras
        if (camera.isPrimary && !comp.isPrimary) {
            auto view = registry.view<components::CameraComponent>();
            for (auto otherEntity : view) {
                if (otherEntity != internal::fromHandle(entity)) {
                    auto& otherComp = view.get<components::CameraComponent>(otherEntity);
                    otherComp.isPrimary = false;
                }
            }
        }
        comp.isPrimary = camera.isPrimary;
        comp.updateProjectionMatrix();

        return true;
    }

    std::optional<EntityHandle> SceneServiceImpl::getPrimaryCamera() const {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::CameraComponent>();

        // Find camera with isPrimary = true
        for (auto entity : view) {
            const auto& comp = view.get<components::CameraComponent>(entity);
            if (comp.isPrimary) {
                return internal::toHandle(entity);
            }
        }

        // Fallback to first camera if no primary is set
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
        
        if (!sceneEntity.hasComponent<components::TransformComponent>()) {
            auto& transform = sceneEntity.addComponent<components::TransformComponent>();
            sceneEntity.addOrReplaceComponent<components::WorldTransformComponent>().worldMatrix = transform.GetMatrix();
        }
        else if (!sceneEntity.hasComponent<components::WorldTransformComponent>()) {
            auto& transform = sceneEntity.getComponent<components::TransformComponent>();
            sceneEntity.addOrReplaceComponent<components::WorldTransformComponent>().worldMatrix = transform.GetMatrix();
        }

        if (!sceneEntity.hasComponent<components::CameraComponent>()) {
            sceneEntity.addComponent<components::CameraComponent>();
            autoAttachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::Camera));
            return true;
        }

        return false;
    }

    void SceneServiceImpl::autoAttachBillboard(EntityHandle entity, uint32_t iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.addComponent<components::BillboardComponent>();
            billboard.iconType = static_cast<components::BillboardIconType>(iconType);
            billboard.editorOnly = true;
            billboard.selectable = true;
        }
    }

    void SceneServiceImpl::autoDetachBillboard(EntityHandle entity, uint32_t iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.getComponent<components::BillboardComponent>();
            // Only remove if it matches the expected icon type (auto-attached billboard)
            if (billboard.iconType == static_cast<components::BillboardIconType>(iconType)) {
                sceneEntity.removeComponent<components::BillboardComponent>();
            }
        }
    }

    bool SceneServiceImpl::removeCameraComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::CameraComponent>()) {
            sceneEntity.removeComponent<components::CameraComponent>();
            autoDetachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::Camera));
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
        data.showBoundingBox = comp.showBoundingBox;

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
            comp.showBoundingBox = mesh.showBoundingBox;
        }
        else {
            auto& comp = sceneEntity.addComponent<components::MeshComponent>();
            comp.meshPath = mesh.meshPath;
            comp.showBoundingBox = mesh.showBoundingBox;
        }

        // Publish notification to allow preloading of mesh assets
        if (!mesh.meshPath.empty()) {
            events::scene::MeshDataChangedNotification notification;
            notification.entity = entity;
            notification.meshPath = mesh.meshPath;
            events::EventDispatcher::instance().publish(notification);
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
            
            events::scene::MeshDataChangedNotification notification;
            notification.entity = entity;
            notification.meshPath = "";
            events::EventDispatcher::instance().publish(notification);

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

    // ========== MATERIAL COMPONENT OPERATIONS ==========

    bool SceneServiceImpl::addMaterialComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            sceneEntity.addComponent<components::MaterialComponent>();
            return true;
        }
        return false;
    }

    bool SceneServiceImpl::removeMaterialComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::MaterialComponent>()) {
            sceneEntity.removeComponent<components::MaterialComponent>();
            return true;
        }
        return false;
    }

    bool SceneServiceImpl::hasMaterialComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::MaterialComponent>();
    }

    std::optional<MaterialData> SceneServiceImpl::getMaterialData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        MaterialData data;
        data.defaultMaterial = comp.defaultMaterial;
        data.subMeshMaterials = comp.subMeshMaterials;
        data.parameterOverrides = comp.parameterOverrides;
        return data;
    }

    bool SceneServiceImpl::setMaterialData(EntityHandle entity, const MaterialData& material) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            sceneEntity.addComponent<components::MaterialComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        comp.defaultMaterial = material.defaultMaterial;
        comp.subMeshMaterials = material.subMeshMaterials;
        comp.parameterOverrides = material.parameterOverrides;
        return true;
    }

    bool SceneServiceImpl::setDefaultMaterial(EntityHandle entity, const std::string& materialPath) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            sceneEntity.addComponent<components::MaterialComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        comp.setDefaultMaterial(materialPath);
        return true;
    }

    bool SceneServiceImpl::setSubMeshMaterial(EntityHandle entity, const std::string& submeshName, const std::string& materialPath) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            sceneEntity.addComponent<components::MaterialComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        if (materialPath.empty()) {
            // Clear the assignment
            comp.subMeshMaterials.erase(submeshName);
        } else {
            comp.setSubMeshMaterial(submeshName, materialPath);
        }
        return true;
    }

    std::string SceneServiceImpl::getSubMeshMaterial(EntityHandle entity, const std::string& submeshName) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return "";
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            return "";
        }

        const auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        return comp.getMaterialForSubmesh(submeshName);
    }

    std::map<std::string, std::string> SceneServiceImpl::getAllSubMeshMaterials(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return {};
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::MaterialComponent>()) {
            return {};
        }

        const auto& comp = sceneEntity.getComponent<components::MaterialComponent>();
        return comp.subMeshMaterials;
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

    // ========== 2D AUDIO SOURCE COMPONENT OPERATIONS ==========

    bool SceneServiceImpl::addAudioSource2DComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
            sceneEntity.addComponent<components::AudioSource2DComponent>();
            autoAttachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::AudioSource));
            return true;
        }
        return false;
    }

    bool SceneServiceImpl::removeAudioSource2DComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
            sceneEntity.removeComponent<components::AudioSource2DComponent>();
            // Only remove billboard if no other audio component exists
            if (!sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
                autoDetachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::AudioSource));
            }
            return true;
        }
        return false;
    }

    bool SceneServiceImpl::hasAudioSource2DComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::AudioSource2DComponent>();
    }

    std::optional<AudioSource2DData> SceneServiceImpl::getAudioSource2DData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::AudioSource2DComponent>();
        AudioSource2DData data;
        data.audioFilePath = comp.audioFilePath;
        data.volume = comp.volume;
        data.pitch = comp.pitch;
        data.loop = comp.loop;
        return data;
    }

    bool SceneServiceImpl::setAudioSource2DData(EntityHandle entity, const AudioSource2DData& audioData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
            sceneEntity.addComponent<components::AudioSource2DComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::AudioSource2DComponent>();
        comp.audioFilePath = audioData.audioFilePath;
        comp.volume = audioData.volume;
        comp.pitch = audioData.pitch;
        comp.loop = audioData.loop;
        return true;
    }

    // ========== 3D AUDIO SOURCE COMPONENT OPERATIONS ==========

    bool SceneServiceImpl::addAudioSource3DComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
            sceneEntity.addComponent<components::AudioSource3DComponent>();
            autoAttachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::AudioSource));
            return true;
        }
        return false;
    }

    bool SceneServiceImpl::removeAudioSource3DComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
            sceneEntity.removeComponent<components::AudioSource3DComponent>();
            // Only remove billboard if no other audio component exists
            if (!sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
                autoDetachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::AudioSource));
            }
            return true;
        }
        return false;
    }

    bool SceneServiceImpl::hasAudioSource3DComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::AudioSource3DComponent>();
    }

    std::optional<AudioSource3DData> SceneServiceImpl::getAudioSource3DData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::AudioSource3DComponent>();
        AudioSource3DData data;
        data.audioFilePath = comp.audioFilePath;
        data.volume = comp.volume;
        data.pitch = comp.pitch;
        data.loop = comp.loop;
        data.minDistance = comp.minDistance;
        data.maxDistance = comp.maxDistance;
        data.showDebugSpheres = comp.showDebugSpheres;
        return data;
    }

    bool SceneServiceImpl::setAudioSource3DData(EntityHandle entity, const AudioSource3DData& audioData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
            sceneEntity.addComponent<components::AudioSource3DComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::AudioSource3DComponent>();
        comp.audioFilePath = audioData.audioFilePath;
        comp.volume = audioData.volume;
        comp.pitch = audioData.pitch;
        comp.loop = audioData.loop;
        comp.minDistance = audioData.minDistance;
        comp.maxDistance = audioData.maxDistance;
        comp.showDebugSpheres = audioData.showDebugSpheres;
        return true;
    }

    bool SceneServiceImpl::setEntityStatic(EntityHandle entity, bool isStatic) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        auto enttEntity = internal::fromHandle(entity);
        if (!registry.all_of<components::TransformComponent>(enttEntity)) {
            return false;
        }

        auto& transform = registry.get<components::TransformComponent>(enttEntity);
        bool wasStatic = transform.isStatic;

        if (isStatic == wasStatic) {
            return true; 
        }

        transform.isStatic = isStatic;
        
        events::scene::EntityStaticChangedNotification notification;
        notification.entity = entity;
        notification.isStatic = isStatic;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    bool SceneServiceImpl::isEntityStatic(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return true;  // Default to static
        }

        auto enttEntity = internal::fromHandle(entity);
        if (!registry.all_of<components::TransformComponent>(enttEntity)) {
            return true;  // Default to static
        }

        const auto& transform = registry.get<components::TransformComponent>(enttEntity);
        return transform.isStatic;
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

        // Publish loading started notification
        events::scene::SceneLoadingStartedNotification startNotif;
        startNotif.scenePath = filePath;
        dispatcher.publish(startNotif);

        // Create progress callback that publishes notifications
        auto progressCallback = [&dispatcher](const std::string& entityName, size_t loaded, size_t total) {
            events::scene::SceneLoadingProgressUpdatedNotification progressNotif;
            progressNotif.currentEntityName = entityName;
            progressNotif.progress = (total > 0) ? static_cast<float>(loaded) / static_cast<float>(total) : 0.0f;
            dispatcher.publish(progressNotif);
        };

        bool success = serialization::SceneSerialization::loadSceneInto(filePath, *sceneGraph, progressCallback);

        // Publish loading completed notification
        events::scene::SceneLoadingCompletedNotification completeNotif;
        completeNotif.scenePath = filePath;
        completeNotif.success = success;
        if (!success) {
            completeNotif.errorMessage = "Failed to load scene file";
        }
        dispatcher.publish(completeNotif);

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

            // Preload meshes for all entities with MeshComponent
            // This is needed because scene loading bypasses setMeshData which normally triggers preloading
            auto& registry = scene::EntityRegistry::getRegistry();
            auto meshView = registry.view<components::MeshComponent>();
            for (auto entity : meshView) {
                const auto& meshComp = meshView.get<components::MeshComponent>(entity);
                if (!meshComp.meshPath.empty()) {
                    events::scene::MeshDataChangedNotification meshNotif;
                    meshNotif.entity = internal::toHandle(entity);
                    meshNotif.meshPath = meshComp.meshPath;
                    dispatcher.publish(meshNotif);
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

        // Static entity handlers
        dispatcher.registerCommandHandler<events::scene::SetEntityStaticCommand>(
            [this](const events::scene::SetEntityStaticCommand& cmd) {
                return setEntityStatic(cmd.entity, cmd.isStatic);
            });

        dispatcher.registerQueryHandler<events::scene::IsEntityStaticQuery>(
            [this](const events::scene::IsEntityStaticQuery& query) {
                return isEntityStatic(query.entity);
            });

        // Material command handlers
        dispatcher.registerCommandHandler<events::material::AddMaterialComponentCommand>(
            [this](const events::material::AddMaterialComponentCommand& cmd) {
                return addMaterialComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::material::RemoveMaterialComponentCommand>(
            [this](const events::material::RemoveMaterialComponentCommand& cmd) {
                return removeMaterialComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::material::SetMaterialDataCommand>(
            [this](const events::material::SetMaterialDataCommand& cmd) {
                return setMaterialData(cmd.entity, cmd.materialData);
            });

        dispatcher.registerCommandHandler<events::material::SetDefaultMaterialCommand>(
            [this](const events::material::SetDefaultMaterialCommand& cmd) {
                return setDefaultMaterial(cmd.entity, cmd.materialPath);
            });

        dispatcher.registerCommandHandler<events::material::SetSubMeshMaterialCommand>(
            [this](const events::material::SetSubMeshMaterialCommand& cmd) {
                return setSubMeshMaterial(cmd.entity, cmd.submeshName, cmd.materialPath);
            });

        // Material query handlers
        dispatcher.registerQueryHandler<events::material::HasMaterialComponentQuery>(
            [this](const events::material::HasMaterialComponentQuery& query) {
                return hasMaterialComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::material::GetMaterialDataQuery>(
            [this](const events::material::GetMaterialDataQuery& query) {
                return getMaterialData(query.entity);
            });

        dispatcher.registerQueryHandler<events::material::GetSubMeshMaterialQuery>(
            [this](const events::material::GetSubMeshMaterialQuery& query) {
                return getSubMeshMaterial(query.entity, query.submeshName);
            });

        dispatcher.registerQueryHandler<events::material::GetAllSubMeshMaterialsQuery>(
            [this](const events::material::GetAllSubMeshMaterialsQuery& query) {
                return getAllSubMeshMaterials(query.entity);
            });

        // 2D Audio Source component handlers
        dispatcher.registerCommandHandler<events::scene::AddAudioSource2DComponentCommand>(
            [this](const events::scene::AddAudioSource2DComponentCommand& cmd) {
                return addAudioSource2DComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveAudioSource2DComponentCommand>(
            [this](const events::scene::RemoveAudioSource2DComponentCommand& cmd) {
                return removeAudioSource2DComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetAudioSource2DDataCommand>(
            [this](const events::scene::SetAudioSource2DDataCommand& cmd) {
                return setAudioSource2DData(cmd.entity, cmd.audioData);
            });

        dispatcher.registerQueryHandler<events::scene::HasAudioSource2DComponentQuery>(
            [this](const events::scene::HasAudioSource2DComponentQuery& query) {
                return hasAudioSource2DComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetAudioSource2DDataQuery>(
            [this](const events::scene::GetAudioSource2DDataQuery& query) {
                return getAudioSource2DData(query.entity);
            });

        // 3D Audio Source component handlers
        dispatcher.registerCommandHandler<events::scene::AddAudioSource3DComponentCommand>(
            [this](const events::scene::AddAudioSource3DComponentCommand& cmd) {
                return addAudioSource3DComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveAudioSource3DComponentCommand>(
            [this](const events::scene::RemoveAudioSource3DComponentCommand& cmd) {
                return removeAudioSource3DComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetAudioSource3DDataCommand>(
            [this](const events::scene::SetAudioSource3DDataCommand& cmd) {
                return setAudioSource3DData(cmd.entity, cmd.audioData);
            });

        dispatcher.registerQueryHandler<events::scene::HasAudioSource3DComponentQuery>(
            [this](const events::scene::HasAudioSource3DComponentQuery& query) {
                return hasAudioSource3DComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetAudioSource3DDataQuery>(
            [this](const events::scene::GetAudioSource3DDataQuery& query) {
                return getAudioSource3DData(query.entity);
            });
    }

}
