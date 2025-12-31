#include "SceneServiceImpl.hpp"
#include "components/CameraComponentService.hpp"
#include "components/MeshComponentService.hpp"
#include "components/MaterialComponentService.hpp"
#include "components/AudioComponentService.hpp"
#include "components/IBLComponentService.hpp"
#include "../../utilities/scene/SceneGraphSystem.hpp"
#include "../../utilities/scene/Entity.hpp"
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"
#include "../../utilities/serialization/SceneSerialization.hpp"
#include "../../utilities/serialization/PrefabSerialization.hpp"
#include "../../utilities/serialization/AsyncSceneLoader.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/SceneEvents.hpp"
#include "../events/RenderEvents.hpp"
#include "../events/MaterialEvents.hpp"
#include "../events/AudioEvents.hpp"
#include "print/EditorLogger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>

namespace services {

    SceneServiceImpl::SceneServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
        , cameraService(std::make_unique<CameraComponentService>(sceneGraph))
        , meshService(std::make_unique<MeshComponentService>(sceneGraph))
        , materialService(std::make_unique<MaterialComponentService>(sceneGraph))
        , audioService(std::make_unique<AudioComponentService>(sceneGraph))
        , iblService(std::make_unique<IBLComponentService>(sceneGraph))
        , asyncSceneLoader(std::make_unique<serialization::AsyncSceneLoader>()) {}

    SceneServiceImpl::~SceneServiceImpl() = default;

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
        return cameraService->getCameraData(entity);
    }

    bool SceneServiceImpl::setCameraData(EntityHandle entity, const CameraData& camera) {
        return cameraService->setCameraData(entity, camera);
    }

    std::optional<EntityHandle> SceneServiceImpl::getPrimaryCamera() const {
        return cameraService->getPrimaryCamera();
    }

    bool SceneServiceImpl::addCameraComponent(EntityHandle entity) {
        return cameraService->addCameraComponent(entity);
    }

    bool SceneServiceImpl::removeCameraComponent(EntityHandle entity) {
        return cameraService->removeCameraComponent(entity);
    }

    std::optional<IBLData> SceneServiceImpl::getIBLData(EntityHandle entity) const {
        return iblService->getIBLData(entity);
    }

    bool SceneServiceImpl::setIBLData(EntityHandle entity, const IBLData& ibl) {
        return iblService->setIBLData(entity, ibl);
    }

    bool SceneServiceImpl::removeIBLComponent(EntityHandle entity) {
        return iblService->removeIBLComponent(entity);
    }

    std::optional<MeshData> SceneServiceImpl::getMeshData(EntityHandle entity) const {
        return meshService->getMeshData(entity);
    }

    bool SceneServiceImpl::setMeshData(EntityHandle entity, const MeshData& mesh) {
        return meshService->setMeshData(entity, mesh);
    }

    bool SceneServiceImpl::addMeshComponent(EntityHandle entity) {
        return meshService->addMeshComponent(entity);
    }

    bool SceneServiceImpl::removeMeshComponent(EntityHandle entity) {
        return meshService->removeMeshComponent(entity);
    }

    bool SceneServiceImpl::hasMeshComponent(EntityHandle entity) const {
        return meshService->hasMeshComponent(entity);
    }

    // ========== MATERIAL COMPONENT OPERATIONS ==========

    bool SceneServiceImpl::addMaterialComponent(EntityHandle entity) {
        return materialService->addMaterialComponent(entity);
    }

    bool SceneServiceImpl::removeMaterialComponent(EntityHandle entity) {
        return materialService->removeMaterialComponent(entity);
    }

    bool SceneServiceImpl::hasMaterialComponent(EntityHandle entity) const {
        return materialService->hasMaterialComponent(entity);
    }

    std::optional<MaterialData> SceneServiceImpl::getMaterialData(EntityHandle entity) const {
        return materialService->getMaterialData(entity);
    }

    bool SceneServiceImpl::setMaterialData(EntityHandle entity, const MaterialData& material) {
        return materialService->setMaterialData(entity, material);
    }

    bool SceneServiceImpl::setDefaultMaterial(EntityHandle entity, const std::string& materialPath) {
        return materialService->setDefaultMaterial(entity, materialPath);
    }

    bool SceneServiceImpl::setSubMeshMaterial(EntityHandle entity, const std::string& submeshName, const std::string& materialPath) {
        return materialService->setSubMeshMaterial(entity, submeshName, materialPath);
    }

    std::string SceneServiceImpl::getSubMeshMaterial(EntityHandle entity, const std::string& submeshName) const {
        return materialService->getSubMeshMaterial(entity, submeshName);
    }

    std::map<std::string, std::string> SceneServiceImpl::getAllSubMeshMaterials(EntityHandle entity) const {
        return materialService->getAllSubMeshMaterials(entity);
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
        return audioService->addAudioSource2DComponent(entity);
    }

    bool SceneServiceImpl::removeAudioSource2DComponent(EntityHandle entity) {
        return audioService->removeAudioSource2DComponent(entity);
    }

    bool SceneServiceImpl::hasAudioSource2DComponent(EntityHandle entity) const {
        return audioService->hasAudioSource2DComponent(entity);
    }

    std::optional<AudioSource2DData> SceneServiceImpl::getAudioSource2DData(EntityHandle entity) const {
        return audioService->getAudioSource2DData(entity);
    }

    bool SceneServiceImpl::setAudioSource2DData(EntityHandle entity, const AudioSource2DData& audioData) {
        return audioService->setAudioSource2DData(entity, audioData);
    }

    // ========== 3D AUDIO SOURCE COMPONENT OPERATIONS ==========

    bool SceneServiceImpl::addAudioSource3DComponent(EntityHandle entity) {
        return audioService->addAudioSource3DComponent(entity);
    }

    bool SceneServiceImpl::removeAudioSource3DComponent(EntityHandle entity) {
        return audioService->removeAudioSource3DComponent(entity);
    }

    bool SceneServiceImpl::hasAudioSource3DComponent(EntityHandle entity) const {
        return audioService->hasAudioSource3DComponent(entity);
    }

    std::optional<AudioSource3DData> SceneServiceImpl::getAudioSource3DData(EntityHandle entity) const {
        return audioService->getAudioSource3DData(entity);
    }

    bool SceneServiceImpl::setAudioSource3DData(EntityHandle entity, const AudioSource3DData& audioData) {
        return audioService->setAudioSource3DData(entity, audioData);
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

    void SceneServiceImpl::loadSceneAsync(const std::string& filePath) {
        if (!asyncSceneLoader) {
            asyncSceneLoader = std::make_unique<serialization::AsyncSceneLoader>();
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Publish loading started notification
        events::scene::SceneLoadingStartedNotification startNotif;
        startNotif.scenePath = filePath;
        dispatcher.publish(startNotif);

        // Set up progress callback
        auto progressCallback = [&dispatcher](const std::string& entityName, size_t loaded, size_t total) {
            events::scene::SceneLoadingProgressUpdatedNotification progressNotif;
            progressNotif.currentEntityName = entityName;
            progressNotif.progress = total > 0 ? static_cast<float>(loaded) / static_cast<float>(total) : 0.0f;
            dispatcher.publish(progressNotif);
        };

        // Set up completion callback
        auto completionCallback = [this, filePath, &dispatcher](bool success, const std::string& errorMessage) {
            events::scene::SceneLoadingCompletedNotification completeNotif;
            completeNotif.scenePath = filePath;
            completeNotif.success = success;
            completeNotif.errorMessage = errorMessage;
            dispatcher.publish(completeNotif);

            if (success) {
                // Clear selection on successful load
                selectedEntity.reset();

                // Get IBL path and set it
                // Note: IBL loading is still synchronous here as it requires OffScreen controller changes
                // The main scene entity loading is async with progress feedback
                std::string iblPath = asyncSceneLoader->getIBLPath();
                if (!iblPath.empty()) {
                    events::render::SetIBLCommand setIblCmd;
                    setIblCmd.hdrPath = iblPath;
                    dispatcher.execute(setIblCmd);
                }

                // Publish scene loaded notification
                events::scene::SceneLoadedNotification loadedNotif;
                loadedNotif.scenePath = filePath;
                dispatcher.publish(loadedNotif);
            }

            asyncSceneLoadInProgress = false;
        };

        asyncSceneLoadInProgress = true;
        asyncSceneLoader->startLoad(filePath, sceneGraph, progressCallback, completionCallback);
    }

    void SceneServiceImpl::cancelSceneLoading() {
        if (asyncSceneLoader) {
            asyncSceneLoader->cancel();
            asyncSceneLoadInProgress = false;
        }
    }

    bool SceneServiceImpl::isSceneLoading() const {
        return asyncSceneLoadInProgress && asyncSceneLoader && asyncSceneLoader->isLoading();
    }

    void SceneServiceImpl::updateAsyncSceneLoading() {
        if (asyncSceneLoadInProgress && asyncSceneLoader) {
            asyncSceneLoader->update();
        }
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

        // Register component service handlers
        cameraService->registerEventHandlers(dispatcher);
        meshService->registerEventHandlers(dispatcher);
        materialService->registerEventHandlers(dispatcher);
        audioService->registerEventHandlers(dispatcher);
        iblService->registerEventHandlers(dispatcher);

        // Core entity command handlers
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

        // Core entity query handlers
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

        dispatcher.registerQueryHandler<events::scene::GetRootEntityQuery>(
            [this](const events::scene::GetRootEntityQuery&) {
                return getRoot();
            });

        // Scene lifecycle handlers
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

        // Async scene loading handlers
        dispatcher.registerCommandHandler<events::scene::LoadSceneAsyncCommand>(
            [this](const events::scene::LoadSceneAsyncCommand& cmd) {
                loadSceneAsync(cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::scene::CancelSceneLoadingCommand>(
            [this](const events::scene::CancelSceneLoadingCommand&) {
                cancelSceneLoading();
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

        // Prefab handlers
        dispatcher.registerCommandHandler<events::scene::SavePrefabCommand>(
            [this](const events::scene::SavePrefabCommand& cmd) {
                return savePrefab(cmd.entity, cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::scene::LoadPrefabCommand>(
            [this](const events::scene::LoadPrefabCommand& cmd) {
                return loadPrefab(cmd.filePath, cmd.parent);
            });
    }

    bool SceneServiceImpl::savePrefab(EntityHandle entity, const std::string& filePath) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        // Prevent saving root entity as prefab
        scene::Entity& root = sceneGraph->GetRoot();
        if (internal::fromHandle(entity) == root.getHandle()) {
            vfLogWarning("Cannot save root entity as prefab");
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        bool success = serialization::PrefabSerialization::savePrefab(sceneEntity, filePath);

        if (success) {
            // Publish notification
            events::scene::PrefabCreatedNotification notification;
            notification.filePath = filePath;
            notification.sourceEntity = entity;
            events::EventDispatcher::instance().publish(notification);
        }

        return success;
    }

    std::optional<EntityHandle> SceneServiceImpl::loadPrefab(const std::string& filePath,
                                                              std::optional<EntityHandle> parent) {
        // Determine parent entity
        scene::Entity parentEntity = sceneGraph->GetRoot();
        if (parent.has_value() && parent->isValid()) {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (internal::isValidHandle(*parent, registry)) {
                parentEntity = scene::Entity(internal::fromHandle(*parent));
            }
        }

        auto result = serialization::PrefabSerialization::loadPrefab(filePath, parentEntity, *sceneGraph);

        if (result.has_value()) {
            auto handle = internal::toHandle(result->getHandle());
            auto& dispatcher = events::EventDispatcher::instance();

            // Trigger resource loading for all entities in the prefab tree
            // This is needed because prefab loading bypasses service methods which normally trigger preloading
            std::function<void(scene::Entity&)> triggerResourceLoading = [&](scene::Entity& entity) {
                // Mesh loading
                if (entity.hasComponent<components::MeshComponent>()) {
                    const auto& meshComp = entity.getComponent<components::MeshComponent>();
                    if (!meshComp.meshPath.empty()) {
                        events::scene::MeshDataChangedNotification meshNotif;
                        meshNotif.entity = internal::toHandle(entity.getHandle());
                        meshNotif.meshPath = meshComp.meshPath;
                        dispatcher.publish(meshNotif);
                    }
                }

                // Recurse into children
                for (auto& child : entity.getChildren()) {
                    triggerResourceLoading(child);
                }
            };
            triggerResourceLoading(*result);
            
            events::scene::PrefabInstantiatedNotification notification;
            notification.filePath = filePath;
            notification.rootEntity = handle;
            dispatcher.publish(notification);

            return handle;
        }

        return std::nullopt;
    }

}
