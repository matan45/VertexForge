#include "SceneServiceImpl.hpp"
#include "components/CameraComponentService.hpp"
#include "components/MeshComponentService.hpp"
#include "components/MaterialComponentService.hpp"
#include "components/AudioComponentService.hpp"
#include "components/IBLComponentService.hpp"
#include "components/PhysicsComponentService.hpp"
#include "components/AnimatorComponentService.hpp"
#include "scene/HierarchyService.hpp"
#include "scene/EntityQueryService.hpp"
#include "scene/TransformComponentService.hpp"
#include "scene/EntityStateService.hpp"
#include "scene/ScenePersistenceService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "../events/EventDispatcher.hpp"

namespace services
{
    SceneServiceImpl::SceneServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(sceneGraph)
        // Existing component services
        , cameraService(std::make_unique<CameraComponentService>(sceneGraph))
        , meshService(std::make_unique<MeshComponentService>(sceneGraph))
        , materialService(std::make_unique<MaterialComponentService>(sceneGraph))
        , audioService(std::make_unique<AudioComponentService>(sceneGraph))
        , iblService(std::make_unique<IBLComponentService>(sceneGraph))
        , physicsService(std::make_unique<PhysicsComponentService>(sceneGraph))
        , animatorService(std::make_unique<AnimatorComponentService>())
        // New extracted services
        , hierarchyService(std::make_unique<HierarchyService>(sceneGraph))
        , entityQueryService(std::make_unique<EntityQueryService>(sceneGraph))
        , transformService(std::make_unique<TransformComponentService>(sceneGraph))
        , entityStateService(std::make_unique<EntityStateService>(sceneGraph))
        , persistenceService(std::make_unique<ScenePersistenceService>(sceneGraph, entityStateService.get()))
    {
    }

    SceneServiceImpl::~SceneServiceImpl() = default;

    void SceneServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Existing component services
        cameraService->registerEventHandlers(dispatcher);
        meshService->registerEventHandlers(dispatcher);
        materialService->registerEventHandlers(dispatcher);
        audioService->registerEventHandlers(dispatcher);
        iblService->registerEventHandlers(dispatcher);
        physicsService->registerEventHandlers(dispatcher);
        animatorService->registerEventHandlers(dispatcher);

        // New extracted services
        hierarchyService->registerEventHandlers(dispatcher);
        entityQueryService->registerEventHandlers(dispatcher);
        transformService->registerEventHandlers(dispatcher);
        entityStateService->registerEventHandlers(dispatcher);
        persistenceService->registerEventHandlers(dispatcher);
    }

    // ========== HIERARCHY SERVICE DELEGATES ==========

    EntityHandle SceneServiceImpl::createEntity(const std::string& name, std::optional<EntityHandle> parent)
    {
        return hierarchyService->createEntity(name, parent);
    }

    bool SceneServiceImpl::deleteEntity(EntityHandle entity, bool deleteChildren)
    {
        return hierarchyService->deleteEntity(entity, deleteChildren);
    }

    EntityHandle SceneServiceImpl::duplicateEntity(EntityHandle entity)
    {
        return hierarchyService->duplicateEntity(entity);
    }

    bool SceneServiceImpl::reparentEntity(EntityHandle entity, EntityHandle newParent)
    {
        return hierarchyService->reparentEntity(entity, newParent);
    }

    bool SceneServiceImpl::moveEntity(EntityHandle entity, EntityHandle targetParent, int insertIndex)
    {
        return hierarchyService->moveEntity(entity, targetParent, insertIndex);
    }

    EntityHandle SceneServiceImpl::getRoot() const
    {
        return hierarchyService->getRoot();
    }

    std::vector<EntityHandle> SceneServiceImpl::getChildren(EntityHandle entity) const
    {
        return hierarchyService->getChildren(entity);
    }

    // ========== ENTITY QUERY SERVICE DELEGATES ==========

    std::optional<EntityData> SceneServiceImpl::getEntity(EntityHandle handle) const
    {
        return entityQueryService->getEntity(handle);
    }

    std::optional<EntityHandle> SceneServiceImpl::findEntityByName(const std::string& name) const
    {
        return entityQueryService->findEntityByName(name);
    }

    std::vector<EntityHandle> SceneServiceImpl::findEntitiesByName(const std::string& name) const
    {
        return entityQueryService->findEntitiesByName(name);
    }

    std::vector<EntityHandle> SceneServiceImpl::getEntitiesWithComponent(ComponentTypeId type) const
    {
        return entityQueryService->getEntitiesWithComponent(type);
    }

    SceneHierarchyData SceneServiceImpl::getSceneHierarchy() const
    {
        return entityQueryService->getSceneHierarchy();
    }

    bool SceneServiceImpl::hasComponent(EntityHandle entity, ComponentTypeId type) const
    {
        return entityQueryService->hasComponent(entity, type);
    }

    std::vector<ComponentTypeId> SceneServiceImpl::getComponentTypes(EntityHandle entity) const
    {
        return entityQueryService->getComponentTypes(entity);
    }

    // ========== TRANSFORM SERVICE DELEGATES ==========

    void SceneServiceImpl::setTransform(EntityHandle entity, const TransformData& transform)
    {
        transformService->setTransform(entity, transform);
    }

    std::optional<TransformData> SceneServiceImpl::getTransform(EntityHandle entity) const
    {
        return transformService->getTransform(entity);
    }

    std::optional<TransformData> SceneServiceImpl::getWorldTransform(EntityHandle entity) const
    {
        return transformService->getWorldTransform(entity);
    }

    // ========== ENTITY STATE SERVICE DELEGATES ==========

    void SceneServiceImpl::setSelectedEntity(std::optional<EntityHandle> entity)
    {
        entityStateService->setSelectedEntity(entity);
    }

    std::optional<EntityHandle> SceneServiceImpl::getSelectedEntity() const
    {
        return entityStateService->getSelectedEntity();
    }

    std::string SceneServiceImpl::getEntityName(EntityHandle entity) const
    {
        return entityStateService->getEntityName(entity);
    }

    void SceneServiceImpl::setEntityName(EntityHandle entity, const std::string& name)
    {
        entityStateService->setEntityName(entity, name);
    }

    void SceneServiceImpl::setEntityActive(EntityHandle entity, bool isActive)
    {
        entityStateService->setEntityActive(entity, isActive);
    }

    bool SceneServiceImpl::setEntityStatic(EntityHandle entity, bool isStatic)
    {
        return entityStateService->setEntityStatic(entity, isStatic);
    }

    bool SceneServiceImpl::isEntityStatic(EntityHandle entity) const
    {
        return entityStateService->isEntityStatic(entity);
    }

    // ========== PERSISTENCE SERVICE DELEGATES ==========

    bool SceneServiceImpl::newScene()
    {
        return persistenceService->newScene();
    }

    bool SceneServiceImpl::saveScene(const std::string& filePath)
    {
        return persistenceService->saveScene(filePath);
    }

    bool SceneServiceImpl::loadScene(const std::string& filePath)
    {
        return persistenceService->loadScene(filePath);
    }

    bool SceneServiceImpl::savePrefab(EntityHandle entity, const std::string& filePath)
    {
        return persistenceService->savePrefab(entity, filePath);
    }

    std::optional<EntityHandle> SceneServiceImpl::loadPrefab(const std::string& filePath,
                                                             std::optional<EntityHandle> parent)
    {
        return persistenceService->loadPrefab(filePath, parent);
    }

    // ========== CAMERA SERVICE DELEGATES ==========

    std::optional<CameraData> SceneServiceImpl::getCameraData(EntityHandle entity) const
    {
        return cameraService->getCameraData(entity);
    }

    bool SceneServiceImpl::setCameraData(EntityHandle entity, const CameraData& camera)
    {
        return cameraService->setCameraData(entity, camera);
    }

    std::optional<EntityHandle> SceneServiceImpl::getPrimaryCamera() const
    {
        return cameraService->getPrimaryCamera();
    }

    bool SceneServiceImpl::addCameraComponent(EntityHandle entity)
    {
        return cameraService->addCameraComponent(entity);
    }

    bool SceneServiceImpl::removeCameraComponent(EntityHandle entity)
    {
        return cameraService->removeCameraComponent(entity);
    }

    // ========== IBL SERVICE DELEGATES ==========

    std::optional<IBLData> SceneServiceImpl::getIBLData(EntityHandle entity) const
    {
        return iblService->getIBLData(entity);
    }

    bool SceneServiceImpl::setIBLData(EntityHandle entity, const IBLData& ibl)
    {
        return iblService->setIBLData(entity, ibl);
    }

    bool SceneServiceImpl::removeIBLComponent(EntityHandle entity)
    {
        return iblService->removeIBLComponent(entity);
    }

    // ========== MESH SERVICE DELEGATES ==========

    std::optional<MeshData> SceneServiceImpl::getMeshData(EntityHandle entity) const
    {
        return meshService->getMeshData(entity);
    }

    bool SceneServiceImpl::setMeshData(EntityHandle entity, const MeshData& mesh)
    {
        return meshService->setMeshData(entity, mesh);
    }

    bool SceneServiceImpl::addMeshComponent(EntityHandle entity)
    {
        return meshService->addMeshComponent(entity);
    }

    bool SceneServiceImpl::removeMeshComponent(EntityHandle entity)
    {
        return meshService->removeMeshComponent(entity);
    }

    bool SceneServiceImpl::hasMeshComponent(EntityHandle entity) const
    {
        return meshService->hasMeshComponent(entity);
    }

    // ========== MATERIAL SERVICE DELEGATES ==========

    bool SceneServiceImpl::addMaterialComponent(EntityHandle entity)
    {
        return materialService->addMaterialComponent(entity);
    }

    bool SceneServiceImpl::removeMaterialComponent(EntityHandle entity)
    {
        return materialService->removeMaterialComponent(entity);
    }

    bool SceneServiceImpl::hasMaterialComponent(EntityHandle entity) const
    {
        return materialService->hasMaterialComponent(entity);
    }

    std::optional<MaterialData> SceneServiceImpl::getMaterialData(EntityHandle entity) const
    {
        return materialService->getMaterialData(entity);
    }

    bool SceneServiceImpl::setMaterialData(EntityHandle entity, const MaterialData& material)
    {
        return materialService->setMaterialData(entity, material);
    }

    bool SceneServiceImpl::setDefaultMaterial(EntityHandle entity, const std::string& materialPath)
    {
        return materialService->setDefaultMaterial(entity, materialPath);
    }

    bool SceneServiceImpl::setSubMeshMaterial(EntityHandle entity, const std::string& submeshName,
                                              const std::string& materialPath)
    {
        return materialService->setSubMeshMaterial(entity, submeshName, materialPath);
    }

    std::string SceneServiceImpl::getSubMeshMaterial(EntityHandle entity, const std::string& submeshName) const
    {
        return materialService->getSubMeshMaterial(entity, submeshName);
    }

    std::map<std::string, std::string> SceneServiceImpl::getAllSubMeshMaterials(EntityHandle entity) const
    {
        return materialService->getAllSubMeshMaterials(entity);
    }

    // ========== AUDIO SERVICE DELEGATES ==========

    bool SceneServiceImpl::addAudioSource2DComponent(EntityHandle entity)
    {
        return audioService->addAudioSource2DComponent(entity);
    }

    bool SceneServiceImpl::removeAudioSource2DComponent(EntityHandle entity)
    {
        return audioService->removeAudioSource2DComponent(entity);
    }

    bool SceneServiceImpl::hasAudioSource2DComponent(EntityHandle entity) const
    {
        return audioService->hasAudioSource2DComponent(entity);
    }

    std::optional<AudioSource2DData> SceneServiceImpl::getAudioSource2DData(EntityHandle entity) const
    {
        return audioService->getAudioSource2DData(entity);
    }

    bool SceneServiceImpl::setAudioSource2DData(EntityHandle entity, const AudioSource2DData& audioData)
    {
        return audioService->setAudioSource2DData(entity, audioData);
    }

    bool SceneServiceImpl::addAudioSource3DComponent(EntityHandle entity)
    {
        return audioService->addAudioSource3DComponent(entity);
    }

    bool SceneServiceImpl::removeAudioSource3DComponent(EntityHandle entity)
    {
        return audioService->removeAudioSource3DComponent(entity);
    }

    bool SceneServiceImpl::hasAudioSource3DComponent(EntityHandle entity) const
    {
        return audioService->hasAudioSource3DComponent(entity);
    }

    std::optional<AudioSource3DData> SceneServiceImpl::getAudioSource3DData(EntityHandle entity) const
    {
        return audioService->getAudioSource3DData(entity);
    }

    bool SceneServiceImpl::setAudioSource3DData(EntityHandle entity, const AudioSource3DData& audioData)
    {
        return audioService->setAudioSource3DData(entity, audioData);
    }
}
