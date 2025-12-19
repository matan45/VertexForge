#pragma once
#include "../interfaces/ISceneService.hpp"
#include "../events/SceneEvents.hpp"
#include "../data/EntityConversion.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <optional>
#include <cstdint>

namespace scene {
    class SceneGraphSystem;
}

namespace services {

    class SceneServiceImpl : public ISceneService {
    public:
        explicit SceneServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~SceneServiceImpl() override = default;
        
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
        bool setSubMeshMaterial(EntityHandle entity, const std::string& submeshName, const std::string& materialPath) override;
        std::string getSubMeshMaterial(EntityHandle entity, const std::string& submeshName) const override;
        std::map<std::string, std::string> getAllSubMeshMaterials(EntityHandle entity) const override;

        // Hierarchy - Children
        std::vector<EntityHandle> getChildren(EntityHandle entity) const override;

        // Selection State
        void setSelectedEntity(std::optional<EntityHandle> entity) override;
        std::optional<EntityHandle> getSelectedEntity() const override;

        // Entity Naming
        std::string getEntityName(EntityHandle entity) const override;
        void setEntityName(EntityHandle entity, const std::string& name) override;

        // Scene Lifecycle
        bool newScene();
        bool saveScene(const std::string& filePath);
        bool loadScene(const std::string& filePath);

    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        std::optional<EntityHandle> selectedEntity;
        
        EntityData buildEntityData(entt::entity entity) const;

        // Recursive helper for scene hierarchy
        void collectHierarchy(entt::entity entity, std::vector<EntityData>& entities) const;
        
        void autoAttachBillboard(EntityHandle entity, uint32_t iconType);
    };

}
