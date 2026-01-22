#pragma once
#include "../interfaces/ISceneService.hpp"
#include "../events/SceneEvents.hpp"
#include "../data/EntityConversion.hpp"
#include <memory>
#include <optional>
#include <cstdint>

namespace scene
{
    class SceneGraphSystem;
}

namespace services
{
    class IAnimatorProvider;

    // Existing component services
    class CameraComponentService;
    class MeshComponentService;
    class MaterialComponentService;
    class AudioComponentService;
    class IBLComponentService;
    class PhysicsComponentService;
    class AnimatorComponentService;
    class VFXComponentService;

    // New extracted services
    class HierarchyService;
    class EntityQueryService;
    class TransformComponentService;
    class EntityStateService;
    class ScenePersistenceService;

    class SceneServiceImpl : public ISceneService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        // Existing component services
        std::unique_ptr<CameraComponentService> cameraService;
        std::unique_ptr<MeshComponentService> meshService;
        std::unique_ptr<MaterialComponentService> materialService;
        std::unique_ptr<AudioComponentService> audioService;
        std::unique_ptr<IBLComponentService> iblService;
        std::unique_ptr<PhysicsComponentService> physicsService;
        std::unique_ptr<AnimatorComponentService> animatorService;
        std::unique_ptr<VFXComponentService> vfxService;

        // New extracted services
        std::unique_ptr<HierarchyService> hierarchyService;
        std::unique_ptr<EntityQueryService> entityQueryService;
        std::unique_ptr<TransformComponentService> transformService;
        std::unique_ptr<EntityStateService> entityStateService;
        std::unique_ptr<ScenePersistenceService> persistenceService;

    public:
        explicit SceneServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                                  IAnimatorProvider* animatorProvider);
        ~SceneServiceImpl() override;

        void registerEventHandlers() override;

        // Entity Lifecycle
        EntityHandle createEntity(const std::string& name,
                                  std::optional<EntityHandle> parent = std::nullopt) override;
        bool deleteEntity(EntityHandle entity, bool deleteChildren = true) override;
        EntityHandle duplicateEntity(EntityHandle entity) override;

        // Hierarchy Operations
        bool reparentEntity(EntityHandle entity, EntityHandle newParent) override;
        bool moveEntity(EntityHandle entity, EntityHandle targetParent, int insertIndex) override;
        EntityHandle getRoot() const override;

        // Entity Queries
        std::optional<EntityData> getEntity(EntityHandle handle) const override;
        std::optional<EntityHandle> findEntityByName(const std::string& name) const override;
        std::vector<EntityHandle> findEntitiesByName(const std::string& name) const override;
        std::vector<EntityHandle> getEntitiesWithComponent(ComponentTypeId type) const override;
        SceneHierarchyData getSceneHierarchy() const override;

        // Transform Operations
        void setTransform(EntityHandle entity, const TransformData& transform) override;
        std::optional<TransformData> getTransform(EntityHandle entity) const override;
        std::optional<TransformData> getWorldTransform(EntityHandle entity) const override;

        // Component Queries
        bool hasComponent(EntityHandle entity, ComponentTypeId type) const override;
        std::vector<ComponentTypeId> getComponentTypes(EntityHandle entity) const override;

        // Camera Operations
        std::optional<CameraData> getCameraData(EntityHandle entity) const override;
        bool setCameraData(EntityHandle entity, const CameraData& camera) override;
        std::optional<EntityHandle> getPrimaryCamera() const override;
        bool addCameraComponent(EntityHandle entity) override;
        bool removeCameraComponent(EntityHandle entity) override;

        // IBL Operations
        std::optional<IBLData> getIBLData(EntityHandle entity) const override;
        bool setIBLData(EntityHandle entity, const IBLData& ibl) override;
        bool removeIBLComponent(EntityHandle entity) override;

        // Mesh Operations
        std::optional<MeshData> getMeshData(EntityHandle entity) const override;
        bool setMeshData(EntityHandle entity, const MeshData& mesh) override;
        bool addMeshComponent(EntityHandle entity) override;
        bool removeMeshComponent(EntityHandle entity) override;
        bool hasMeshComponent(EntityHandle entity) const override;

        // Material Operations
        bool addMaterialComponent(EntityHandle entity) override;
        bool removeMaterialComponent(EntityHandle entity) override;
        bool hasMaterialComponent(EntityHandle entity) const override;
        std::optional<MaterialData> getMaterialData(EntityHandle entity) const override;
        bool setMaterialData(EntityHandle entity, const MaterialData& material) override;
        bool setDefaultMaterial(EntityHandle entity, const std::string& materialPath) override;
        bool setSubMeshMaterial(EntityHandle entity, const std::string& submeshName,
                                const std::string& materialPath) override;
        std::string getSubMeshMaterial(EntityHandle entity, const std::string& submeshName) const override;
        std::map<std::string, std::string> getAllSubMeshMaterials(EntityHandle entity) const override;

        // Hierarchy - Children
        std::vector<EntityHandle> getChildren(EntityHandle entity) const override;

        // 2D Audio Source Operations (streaming, for background music)
        bool addAudioSource2DComponent(EntityHandle entity) override;
        bool removeAudioSource2DComponent(EntityHandle entity) override;
        bool hasAudioSource2DComponent(EntityHandle entity) const override;
        std::optional<AudioSource2DData> getAudioSource2DData(EntityHandle entity) const override;
        bool setAudioSource2DData(EntityHandle entity, const AudioSource2DData& audioData) override;

        // 3D Audio Source Operations (cached, for spatial sound effects)
        bool addAudioSource3DComponent(EntityHandle entity) override;
        bool removeAudioSource3DComponent(EntityHandle entity) override;
        bool hasAudioSource3DComponent(EntityHandle entity) const override;
        std::optional<AudioSource3DData> getAudioSource3DData(EntityHandle entity) const override;
        bool setAudioSource3DData(EntityHandle entity, const AudioSource3DData& audioData) override;

        // VFX Component Operations
        bool addVFXComponent(EntityHandle entity) override;
        bool removeVFXComponent(EntityHandle entity) override;
        bool hasVFXComponent(EntityHandle entity) const override;
        std::optional<VFXData> getVFXData(EntityHandle entity) const override;
        bool setVFXData(EntityHandle entity, const VFXData& vfxData) override;

        // Static Entity Operations
        bool setEntityStatic(EntityHandle entity, bool isStatic) override;
        bool isEntityStatic(EntityHandle entity) const override;

        // Selection State
        void setSelectedEntity(std::optional<EntityHandle> entity) override;
        std::optional<EntityHandle> getSelectedEntity() const override;

        // Entity Naming
        std::string getEntityName(EntityHandle entity) const override;
        void setEntityName(EntityHandle entity, const std::string& name) override;

        // Entity Active State
        void setEntityActive(EntityHandle entity, bool isActive);

        // Scene Lifecycle
        bool newScene();
        bool saveScene(const std::string& filePath);
        bool loadScene(const std::string& filePath);

        // Prefab Operations
        bool savePrefab(EntityHandle entity, const std::string& filePath);
        std::optional<EntityHandle> loadPrefab(const std::string& filePath,
                                               std::optional<EntityHandle> parent = std::nullopt);
    };
}
