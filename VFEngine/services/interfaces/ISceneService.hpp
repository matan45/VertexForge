#pragma once
#include "../data/EntityHandle.hpp"
#include "../data/DTOs.hpp"
#include "IAudioService.hpp"
#include <optional>
#include <vector>
#include <string>

namespace services {

    class ISceneService {
    public:
        virtual ~ISceneService() = default;

        virtual void registerEventHandlers() = 0;

        // Per-frame update for deferred operations (e.g. scene loading)
        virtual void update() {}

        // ============================================
        // Entity Lifecycle
        // ============================================

        virtual EntityHandle createEntity(const std::string& name,
            std::optional<EntityHandle> parent = std::nullopt) = 0;

        virtual bool deleteEntity(EntityHandle entity, bool deleteChildren = true) = 0;

        virtual EntityHandle duplicateEntity(EntityHandle entity) = 0;

        // ============================================
        // Hierarchy Operations
        // ============================================

        virtual bool reparentEntity(EntityHandle entity, EntityHandle newParent) = 0;

        virtual bool moveEntity(EntityHandle entity, EntityHandle targetParent, int insertIndex) = 0;

        virtual EntityHandle getRoot() const = 0;

        virtual std::vector<EntityHandle> getChildren(EntityHandle entity) const = 0;

        // ============================================
        // Entity Queries
        // ============================================

        virtual std::optional<EntityData> getEntity(EntityHandle handle) const = 0;

        virtual std::optional<EntityHandle> findEntityByName(const std::string& name) const = 0;

        virtual std::vector<EntityHandle> findEntitiesByName(const std::string& name) const = 0;

        virtual std::vector<EntityHandle> getEntitiesWithComponent(ComponentTypeId type) const = 0;

        virtual SceneHierarchyData getSceneHierarchy() const = 0;

        // ============================================
        // Transform Operations
        // ============================================

        virtual void setTransform(EntityHandle entity, const TransformData& transform) = 0;

        virtual std::optional<TransformData> getTransform(EntityHandle entity) const = 0;

        virtual std::optional<TransformData> getWorldTransform(EntityHandle entity) const = 0;

        // ============================================
        // Component Queries
        // ============================================

        virtual bool hasComponent(EntityHandle entity, ComponentTypeId type) const = 0;

        virtual std::vector<ComponentTypeId> getComponentTypes(EntityHandle entity) const = 0;

        // ============================================
        // Camera Operations
        // ============================================

        virtual std::optional<CameraData> getCameraData(EntityHandle entity) const = 0;

        virtual bool setCameraData(EntityHandle entity, const CameraData& camera) = 0;

        virtual std::optional<EntityHandle> getPrimaryCamera() const = 0;

        virtual bool addCameraComponent(EntityHandle entity) = 0;

        virtual bool removeCameraComponent(EntityHandle entity) = 0;

        // ============================================
        // IBL Component Operations
        // ============================================

        virtual std::optional<IBLData> getIBLData(EntityHandle entity) const = 0;

        virtual bool setIBLData(EntityHandle entity, const IBLData& ibl) = 0;

        virtual bool removeIBLComponent(EntityHandle entity) = 0;

        // ============================================
        // Mesh Component Operations
        // ============================================

        virtual std::optional<MeshData> getMeshData(EntityHandle entity) const = 0;

        virtual bool setMeshData(EntityHandle entity, const MeshData& mesh) = 0;

        virtual bool addMeshComponent(EntityHandle entity) = 0;

        virtual bool removeMeshComponent(EntityHandle entity) = 0;

        virtual bool hasMeshComponent(EntityHandle entity) const = 0;

        // ============================================
        // Material Component Operations
        // ============================================

        virtual bool addMaterialComponent(EntityHandle entity) = 0;

        virtual bool removeMaterialComponent(EntityHandle entity) = 0;

        virtual bool hasMaterialComponent(EntityHandle entity) const = 0;

        virtual std::optional<MaterialData> getMaterialData(EntityHandle entity) const = 0;

        virtual bool setMaterialData(EntityHandle entity, const MaterialData& material) = 0;

        virtual bool setDefaultMaterial(EntityHandle entity, const std::string& materialPath) = 0;

        virtual bool setSubMeshMaterial(EntityHandle entity, const std::string& submeshName, const std::string& materialPath) = 0;

        virtual std::string getSubMeshMaterial(EntityHandle entity, const std::string& submeshName) const = 0;

        virtual std::map<std::string, std::string> getAllSubMeshMaterials(EntityHandle entity) const = 0;

        // ============================================
        // 2D Audio Source Component Operations
        // ============================================

        virtual bool addAudioSource2DComponent(EntityHandle entity) = 0;

        virtual bool removeAudioSource2DComponent(EntityHandle entity) = 0;

        virtual bool hasAudioSource2DComponent(EntityHandle entity) const = 0;

        virtual std::optional<AudioSource2DData> getAudioSource2DData(EntityHandle entity) const = 0;

        virtual bool setAudioSource2DData(EntityHandle entity, const AudioSource2DData& audioData) = 0;

        // ============================================
        // 3D Audio Source Component Operations
        // ============================================

        virtual bool addAudioSource3DComponent(EntityHandle entity) = 0;

        virtual bool removeAudioSource3DComponent(EntityHandle entity) = 0;

        virtual bool hasAudioSource3DComponent(EntityHandle entity) const = 0;

        virtual std::optional<AudioSource3DData> getAudioSource3DData(EntityHandle entity) const = 0;

        virtual bool setAudioSource3DData(EntityHandle entity, const AudioSource3DData& audioData) = 0;

        // ============================================
        // VFX Component Operations
        // ============================================

        virtual bool addVFXComponent(EntityHandle entity) = 0;

        virtual bool removeVFXComponent(EntityHandle entity) = 0;

        virtual bool hasVFXComponent(EntityHandle entity) const = 0;

        virtual std::optional<VFXData> getVFXData(EntityHandle entity) const = 0;

        virtual bool setVFXData(EntityHandle entity, const VFXData& vfxData) = 0;

        // ============================================
        // Billboard Component Operations
        // ============================================

        virtual bool addBillboardComponent(EntityHandle entity) = 0;

        virtual bool removeBillboardComponent(EntityHandle entity) = 0;

        virtual bool hasBillboardComponent(EntityHandle entity) const = 0;

        virtual std::optional<BillboardData> getBillboardData(EntityHandle entity) const = 0;

        virtual bool setBillboardData(EntityHandle entity, const BillboardData& billboardData) = 0;

        // ============================================
        // Static Entity Operations
        // ============================================

        virtual bool setEntityStatic(EntityHandle entity, bool isStatic) = 0;

        virtual bool isEntityStatic(EntityHandle entity) const = 0;

        // ============================================
        // Selection State
        // ============================================

        virtual void setSelectedEntity(std::optional<EntityHandle> entity) = 0;

        virtual std::optional<EntityHandle> getSelectedEntity() const = 0;

        // ============================================
        // Entity Naming
        // ============================================

        virtual std::string getEntityName(EntityHandle entity) const = 0;

        virtual void setEntityName(EntityHandle entity, const std::string& name) = 0;
    };

}
